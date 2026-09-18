#pragma once
#include "ArdaDependencyNode.h"
#include "RHI/Shaders/ArdaComputeOperand.h"
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
		using TArdaValue = T;
		template <class T, EArdaComputeAccess A>
		using TArdaBuffer = FArdaDependencyCudaBuffer;
		template <class T, EArdaComputeAccess A>
		using TArdaSurface = FArdaDependencyCudaSurface;
	};
	template <class P>
	using TArdaDependencyCudaParameters = typename P::template TArdaRebind<FArdaDependencyCudaDomain>;

	struct FArdaDependencyCudaVisitors
	{
		struct FArdaDescribe
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

		struct FArdaKey
		{
			const uint8_t* mBytes;
			FArdaDependencyKeyBuilder mKey;

			template <class T>
			void Value(const char*, size_t Offset)
			{
				static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
				    "Register a custom canonical-key visitor for aggregate CUDA values.");
				mKey.Value(*reinterpret_cast<const T*>(mBytes + Offset));
			}

			template <class T, EArdaComputeAccess A>
			void Buffer(const char*, size_t Offset)
			{
				const auto& V = *reinterpret_cast<const FArdaDependencyCudaBuffer*>(mBytes + Offset);
				mKey.Resource(V.mResource).BufferRange(V.mRange);
			}

			template <class T, EArdaComputeAccess A>
			void Surface(const char*, size_t Offset, EArdaRHIFormat)
			{
				const auto& V = *reinterpret_cast<const FArdaDependencyCudaSurface*>(mBytes + Offset);
				mKey.Resource(V.mResource).TextureRange(V.mRange);
			}
		};

		struct FArdaResolve
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

	/** Device-bound operation and logical graph arguments owned by one CUDA node type.
	 * Node keeps attachment schemas distinct even when multiple nodes use the same operand type.
	 */
	template <class Node, class Operand>
	struct TArdaDependencyCudaOperandParameters
	{
		/** Retained device-bound operand invoked by this node. */
		eastl::shared_ptr<Operand> mOperation;
		/** Logical resource arguments and scalar values frozen when this node is attached. */
		TArdaDependencyCudaParameters<typename Operand::FArdaParameters> mArguments;
	};

	/** Common node specialization for compiled kernels and external library calls.
	 * Derived supplies GetMetadata; inherited hooks infer schema dependencies and append operations to
	 * ArdaInductor's CUDA sequence. The device-bound operand is an attachment parameter, not a second
	 * registration API. Its identity participates in AttachOrFind keys. Resource views resolve per execution.
	 */
	template <class Derived, class Operand>
	class TArdaCudaOperandNode
	    : public TArdaCudaDependencyNode<Derived, TArdaDependencyCudaOperandParameters<Derived, Operand>>
	{
	public:
		/** Attachment parameters specific to Derived; distinct nodes cannot interchange them. */
		using FArdaParameters = TArdaDependencyCudaOperandParameters<Derived, Operand>;
		using FArdaHostParameters = typename Operand::FArdaParameters;
		using FArdaGraphArguments = TArdaDependencyCudaParameters<FArdaHostParameters>;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters)
		{
			// Admission inspects capabilities and the operand without materializing resources or selecting a kernel.
			FArdaDependencyNodeRequirements Requirements;
			Requirements.mbRequireCuda = true;
			for (const auto& Member : FArdaHostParameters::GetCudaMetadata().GetMembers())
			{
				Requirements.mbRequireCudaSurfaces |= Member.mKind == EArdaComputeParameterKind::Texture;
			}

			if (Parameters.mOperation)
			{
				Requirements.mEnvironment.push_back({"registered CUDA operand",
				    [Operation = Parameters.mOperation](const IArdaRHIDevice& Device)
				    {
					    if (Operation->GetDevice().Get() != &Device)
					    {
						    return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
						        "Registered CUDA operand belongs to another device.");
					    }

					    return Operation->GetOperandSupport();
				    }});
			}

			return Requirements;
		}

		static FArdaRHIStatus Validate(const FArdaParameters& Parameters)
		{
			return Parameters.mOperation
			    ? FArdaRHIStatus{}
			    : FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA nodes require an operand.");
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			FArdaDependencyCudaVisitors::FArdaKey Visitor{reinterpret_cast<const uint8_t*>(&Parameters.mArguments)};
			Visitor.mKey.Value(reinterpret_cast<uintptr_t>(Parameters.mOperation.get()));
			FArdaGraphArguments::VisitMembers(Visitor);
			return Visitor.mKey.Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState&)
		{
			FArdaDependencyCudaVisitors::FArdaDescribe Visitor{
			    reinterpret_cast<const uint8_t*>(&Parameters.mArguments)};
			FArdaGraphArguments::VisitMembers(Visitor);
			return Visitor.mDesc;
		}

		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaCudaSequence& Sequence)
		{
			// Rebind logical versions to this execution's allocations before appending to the shared sequence.
			if (Parameters.mOperation->GetDevice() != Context.GetDevice())
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
				    "Registered CUDA operand belongs to another device.");
			}

			FArdaHostParameters Host;
			FArdaDependencyCudaVisitors::FArdaResolve Visitor{reinterpret_cast<const uint8_t*>(&Parameters.mArguments),
			    reinterpret_cast<uint8_t*>(&Host),
			    FArdaHostParameters::GetCudaMetadata().GetMembers(),
			    Context};
			FArdaGraphArguments::VisitMembers(Visitor);
			return Sequence.Add(*Parameters.mOperation, Host);
		}
	};
}
