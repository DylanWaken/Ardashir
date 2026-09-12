#pragma once
#include "ArdaDependencyGraph.h"
#include "Compute/ArdaComputeOperand.h"
#include <cstring>

namespace arda
{
	/** Buffer version and byte range used by persistent CUDA nodes. */
	struct FArdaDependencyCudaBuffer
	{
		FArdaDependencyResourceHandle mResource;
		FArdaRHIBufferRange mRange;
	};

	/** Texture version and subresource range used by persistent CUDA nodes. */
	struct FArdaDependencyCudaSurface
	{
		FArdaDependencyResourceHandle mResource;
		FArdaRHITextureSubresourceRange mRange;
	};

	/** Rebinds an existing operand schema to persistent logical resource versions. */
	struct FArdaDependencyCudaDomain
	{
		template <class T>
		using Value = T;
		template <class T, EArdaComputeAccess A>
		using Buffer = FArdaDependencyCudaBuffer;
		template <class T, EArdaComputeAccess A>
		using Surface = FArdaDependencyCudaSurface;
	};
	template <class P>
	using TArdaDependencyCudaParameters = typename P::template Rebind<FArdaDependencyCudaDomain>;

	struct FArdaDependencyCudaVisitors
	{
		struct Describe
		{
			const uint8_t* mBytes;
			FArdaDependencyNodeDesc mDesc;

			template <class T>
			void Value(const char*, size_t)
			{
			}

			template <class T, EArdaComputeAccess A>
			void Buffer(const char*, size_t Offset)
			{
				const auto& V = *reinterpret_cast<const FArdaDependencyCudaBuffer*>(mBytes + Offset);
				FArdaDependencyAccess R;
				R.mResource = V.mResource;
				R.mBufferRange = V.mRange;
				R.mState = EArdaRHIResourceState::UnorderedAccess;
				R.mAccess = A == EArdaComputeAccess::Read ? EArdaDependencyAccess::Read
				    : A == EArdaComputeAccess::Write      ? EArdaDependencyAccess::Write
				                                          : EArdaDependencyAccess::ReadWrite;
				mDesc.mAccesses.push_back(R);
			}

			template <class T, EArdaComputeAccess A>
			void Surface(const char*, size_t Offset, EArdaRHIFormat)
			{
				const auto& V = *reinterpret_cast<const FArdaDependencyCudaSurface*>(mBytes + Offset);
				FArdaDependencyAccess R;
				R.mResource = V.mResource;
				R.mTextureRange = V.mRange;
				R.mState = EArdaRHIResourceState::UnorderedAccess;
				R.mAccess = A == EArdaComputeAccess::Read ? EArdaDependencyAccess::Read
				    : A == EArdaComputeAccess::Write      ? EArdaDependencyAccess::Write
				                                          : EArdaDependencyAccess::ReadWrite;
				mDesc.mAccesses.push_back(R);
			}
		};

		struct Key
		{
			const uint8_t* mBytes;
			eastl::string mKey;

			template <class T>
			void Append(T V)
			{
				mKey.append(reinterpret_cast<const char*>(&V), sizeof(V));
			}

			void Resource(FArdaDependencyResourceHandle H)
			{
				Append(H.mGraph);
				Append(H.mIndex);
				Append(H.mGeneration);
			}

			template <class T>
			void Value(const char*, size_t Offset)
			{
				static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
				    "Register a custom canonical-key visitor for aggregate CUDA values.");
				Append(*reinterpret_cast<const T*>(mBytes + Offset));
			}

			template <class T, EArdaComputeAccess A>
			void Buffer(const char*, size_t Offset)
			{
				const auto& V = *reinterpret_cast<const FArdaDependencyCudaBuffer*>(mBytes + Offset);
				Resource(V.mResource);
				Append(V.mRange.mByteOffset);
				Append(V.mRange.mByteSize);
			}

			template <class T, EArdaComputeAccess A>
			void Surface(const char*, size_t Offset, EArdaRHIFormat)
			{
				const auto& V = *reinterpret_cast<const FArdaDependencyCudaSurface*>(mBytes + Offset);
				Resource(V.mResource);
				Append(V.mRange.mBaseMipLevel);
				Append(V.mRange.mMipLevelCount);
				Append(V.mRange.mBaseArraySlice);
				Append(V.mRange.mArraySliceCount);
				Append(V.mRange.mBasePlane);
				Append(V.mRange.mPlaneCount);
			}
		};

		struct Resolve
		{
			const uint8_t* mGraph;
			uint8_t* mHost;
			const eastl::vector<FArdaCudaParameterMember>& mMembers;
			FArdaDependencyExecutionContext& mContext;
			size_t mIndex = 0;

			template <class T>
			void Value(const char*, size_t Offset)
			{
				std::memcpy(mHost + mMembers[mIndex++].mHostOffset, mGraph + Offset, sizeof(T));
			}

			template <class T, EArdaComputeAccess A>
			void Buffer(const char*, size_t Offset)
			{
				const auto& V = *reinterpret_cast<const FArdaDependencyCudaBuffer*>(mGraph + Offset);
				auto& D = *reinterpret_cast<FArdaComputeBufferParameter*>(mHost + mMembers[mIndex++].mHostOffset);
				D = {mContext.GetBuffer(V.mResource), V.mRange};
			}

			template <class T, EArdaComputeAccess A>
			void Surface(const char*, size_t Offset, EArdaRHIFormat)
			{
				const auto& V = *reinterpret_cast<const FArdaDependencyCudaSurface*>(mGraph + Offset);
				auto& D = *reinterpret_cast<FArdaComputeTextureParameter*>(mHost + mMembers[mIndex++].mHostOffset);
				D = {mContext.GetTexture(V.mResource), V.mRange};
			}
		};
	};

	/** Registers a device-bound compiled-kernel or external-call operand with automatic parameter dependencies.
	 * ArdaInductor coalesces these nodes. Authors do not create sequences or manage a CUDA stream.
	 * Different device-bound registrations need distinct names in the global library.
	 */
	template <class Operand>
	FArdaRHIStatus RegisterArdaCudaOperandNode(eastl::string Name, eastl::shared_ptr<Operand> Operation)
	{
		if (!Operation)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "CUDA node registration requires an operand.");
		}
		using P = typename Operand::FParameters;
		using G = TArdaDependencyCudaParameters<P>;
		TArdaDependencyNodeDefinition<G> D;
		D.mName = eastl::move(Name);
		D.mKind = EArdaDependencyNodeKind::Cuda;
		D.mCanonicalKey = [](const G& V)
		{
			FArdaDependencyCudaVisitors::Key K{reinterpret_cast<const uint8_t*>(&V)};
			G::VisitMembers(K);
			return K.mKey;
		};
		D.mDescribe = [](const G& V)
		{
			FArdaDependencyCudaVisitors::Describe Visitor{reinterpret_cast<const uint8_t*>(&V)};
			G::VisitMembers(Visitor);
			return Visitor.mDesc;
		};
		D.mPrepareCuda =
		    [Operation = eastl::move(Operation)](FArdaDependencyExecutionContext& C, const G& V, FArdaCudaSequence& S)
		{
			if (Operation->GetDevice() != C.GetDevice())
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
				    "Registered CUDA operand belongs to another device.");
			}
			P Host;
			FArdaDependencyCudaVisitors::Resolve R{reinterpret_cast<const uint8_t*>(&V),
			    reinterpret_cast<uint8_t*>(&Host),
			    P::GetCudaMetadata().GetMembers(),
			    C};
			G::VisitMembers(R);
			return S.Add(*Operation, Host);
		};
		return FArdaNodeRegistry::Get().Register(eastl::move(D));
	}
}
