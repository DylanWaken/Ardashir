#include "Compute/ArdaCudaParameters.h"

namespace arda
{
	namespace
	{
		eastl::vector<FArdaComputeParameterMember> HostMembers(const eastl::vector<FArdaCudaParameterMember>& Members)
		{
			eastl::vector<FArdaComputeParameterMember> Result;
			Result.reserve(Members.size());
			for (const auto& M : Members)
			{
				Result.push_back({M.mName,
				    "CUDA schema member",
				    M.mKind,
				    M.mAccess,
				    M.mHostOffset,
				    M.mHostSize,
				    M.mHostAlignment,
				    1,
				    M.mHostSize,
				    nullptr});
			}
			return Result;
		}
	}

	FArdaCudaParameterMetadata::FArdaCudaParameterMetadata(const char* Name,
	    size_t HostSize,
	    size_t HostAlignment,
	    FArdaCudaKernelSignature Signature,
	    bool HostLayoutSupported,
	    eastl::vector<FArdaCudaParameterMember> Members)
	    : mSignature(Signature),
	      mMembers(eastl::move(Members)),
	      mHost(Name, HostSize, HostAlignment, HostMembers(mMembers))
	{
		mStatus = mHost.GetStatus();
		if (!mStatus)
		{
			return;
		}
		const auto Invalid = [&](const char* Reason)
		{
			mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Reason);
		};
		if (!HostLayoutSupported || !Signature.mbSupported || !Signature.mType || !Signature.mSize ||
		    !Signature.mAlignment || (Signature.mAlignment & (Signature.mAlignment - 1)))
		{
			Invalid("CUDA parameter schema requires supported host layout and plain CUDA argument storage.");
			return;
		}
		for (size_t I = 0; I < mMembers.size(); ++I)
		{
			const auto& M = mMembers[I];
			if (!M.mbSupported || !M.mCudaSize || M.mCudaOffset > Signature.mSize ||
			    M.mCudaSize > Signature.mSize - M.mCudaOffset || !M.mElementSize || !M.mElementAlignment ||
			    (M.mElementAlignment & (M.mElementAlignment - 1)))
			{
				Invalid("CUDA parameter member has an unsupported type or invalid layout.");
				return;
			}
			if (M.mKind == EArdaComputeParameterKind::Value && M.mHostSize != M.mCudaSize)
			{
				Invalid("CUDA scalar value does not match its device representation.");
				return;
			}
			if (M.mKind != EArdaComputeParameterKind::Value && M.mCudaSize != sizeof(uint64_t))
			{
				Invalid("CUDA resource representation must occupy 64 bits.");
				return;
			}
			if (M.mKind == EArdaComputeParameterKind::Texture)
			{
				const auto F = GetArdaCudaFormatInfo(M.mFormat);
				if (!F.mChannels || size_t(F.mChannels) * F.mBits / 8 != M.mElementSize)
				{
					Invalid("CUDA surface element does not match a supported storage format.");
					return;
				}
			}
			for (size_t J = 0; J < I; ++J)
			{
				if (M.mCudaOffset < mMembers[J].mCudaOffset + mMembers[J].mCudaSize &&
				    mMembers[J].mCudaOffset < M.mCudaOffset + M.mCudaSize)
				{
					Invalid("CUDA parameter fields overlap.");
					return;
				}
			}
		}
	}

	FArdaRHIStatus FArdaCudaParameterMetadata::Prepare(const void* Parameters, FArdaCudaDispatch& Output) const
	{
		if (!mStatus)
		{
			return mStatus;
		}
		if (!Parameters || reinterpret_cast<uintptr_t>(Parameters) % mHost.GetAlignment())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "CUDA host parameter storage is null or misaligned.");
		}
		FArdaCudaDispatch Prepared;
		Prepared.mKernels.resize(1);
		auto& K = Prepared.mKernels.front();
		K.mParameters.resize(mSignature.mSize, 0);
		const auto* Bytes = static_cast<const uint8_t*>(Parameters);
		const auto Fail = [](const char* Reason)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Reason);
		};
		for (const auto& M : mMembers)
		{
			if (M.mKind == EArdaComputeParameterKind::Value)
			{
				std::memcpy(K.mParameters.data() + M.mCudaOffset, Bytes + M.mHostOffset, M.mCudaSize);
				continue;
			}
			FArdaCudaBinding Binding;
			Binding.mAccess = M.mAccess;
			if (M.mKind == EArdaComputeParameterKind::Buffer)
			{
				const auto& P = *reinterpret_cast<const FArdaComputeBufferParameter*>(Bytes + M.mHostOffset);
				if (!P.mBuffer)
				{
					return Fail("CUDA buffer parameter is unbound.");
				}
				const auto& D = P.mBuffer->GetDesc();
				const auto& R = P.mRange;
				if (R.mByteOffset >= D.mByteSize || R.mByteOffset % M.mElementAlignment ||
				    (R.mByteSize != ArdaRHIWholeBuffer &&
				        (!R.mByteSize || R.mByteSize > D.mByteSize - R.mByteOffset)) ||
				    R.Resolve(D).mByteSize % M.mElementSize)
				{
					return Fail("CUDA buffer view has invalid bounds, element count or alignment.");
				}
				Binding.mResource = P.mBuffer;
				Binding.mBufferRange = R;
			}
			else if (M.mKind == EArdaComputeParameterKind::Texture)
			{
				const auto& P = *reinterpret_cast<const FArdaComputeTextureParameter*>(Bytes + M.mHostOffset);
				if (!P.mTexture)
				{
					return Fail("CUDA surface parameter is unbound.");
				}
				const auto& D = P.mTexture->GetDesc();
				const auto& Raw = P.mRange;
				if (Raw.mBaseMipLevel >= D.mMipLevels || Raw.mBaseArraySlice >= D.mArraySize || Raw.mBasePlane != 0 ||
				    (Raw.mMipLevelCount != ArdaRHIAllSubresources &&
				        (!Raw.mMipLevelCount || Raw.mMipLevelCount > D.mMipLevels - Raw.mBaseMipLevel)) ||
				    (Raw.mArraySliceCount != ArdaRHIAllSubresources &&
				        (!Raw.mArraySliceCount || Raw.mArraySliceCount > D.mArraySize - Raw.mBaseArraySlice)) ||
				    (Raw.mPlaneCount != ArdaRHIAllSubresources && Raw.mPlaneCount != 1))
				{
					return Fail("CUDA surface view has invalid mip, layer or plane bounds.");
				}
				const auto R = P.mRange.Resolve(D);
				if (D.mFormat != M.mFormat || R.mMipLevelCount != 1 || R.mBaseArraySlice != 0 ||
				    R.mArraySliceCount != D.mArraySize)
				{
					return Fail("CUDA surface requires its declared format, exactly one mip and all array layers.");
				}
				if (auto S = ValidateArdaCudaTexture(D); !S)
				{
					return S;
				}
				Binding.mResource = P.mTexture;
				Binding.mMipLevel = R.mBaseMipLevel;
			}
			else
			{
				return Fail("Unsupported CUDA parameter resource kind.");
			}

			// The RHI command list validates resource affinity before native recording.
			if (!Binding.mResource->GetCudaResourceInfo().mbSharingEnabled)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    "CUDA parameter resource was not created with CUDA sharing.");
			}
			K.mPatches.push_back({static_cast<uint32_t>(Prepared.mBindings.size()),
			    M.mCudaOffset,
			    M.mKind == EArdaComputeParameterKind::Buffer ? M.mElementAlignment : size_t(1)});
			Prepared.mBindings.push_back(eastl::move(Binding));
		}
		Output = eastl::move(Prepared);
		return {};
	}
}
