/** @file ArdaRHICuda.cpp
 * Shared descriptor, channel-layout and launch checks for native CUDA providers.
 * These checks constrain bindings and launch metadata, not arbitrary kernel memory accesses.
 */
#include "RHI/ArdaRHICuda.h"
#include <exception>

namespace arda
{
	TArdaRHIResult<FArdaCudaTimingResult> FArdaCudaTimingQuery::Poll(bool SubmissionComplete)
	{
		if (!SubmissionComplete)
		{
			return {{}, {}};
		}
		std::unique_lock<std::mutex> Lock(mMutex, std::try_to_lock);
		if (!Lock.owns_lock() || !mPoll)
		{
			return {{}, {}};
		}
		auto Result = mPoll();
		if (!Result || Result.mValue.mbReady)
		{
			mPoll = {};
			mNativeState.reset();
		}
		return Result;
	}

	FArdaCudaGraphCache::FArdaCudaGraphCache(EArdaCudaGraphMode Mode, uint32_t MaximumCachedVariants)
	    : mMode(Mode),
	      mMaximumCachedVariants(MaximumCachedVariants)
	{
	}

	FArdaCudaGraphCache::~FArdaCudaGraphCache() = default;

	EArdaCudaGraphMode FArdaCudaGraphCache::GetMode() const noexcept
	{
		return mMode;
	}

	uint32_t FArdaCudaGraphCache::GetMaximumCachedVariants() const noexcept
	{
		return mMaximumCachedVariants;
	}

	FArdaCudaGraphStats FArdaCudaGraphCache::GetStats() const
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		return mStats;
	}

	void FArdaCudaGraphCache::Reset()
	{
		eastl::shared_ptr<void> Retired;
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			Retired.swap(mNativeState);
			mStats.mCachedVariantCount = 0;
		}
	}

	bool FArdaCudaArchitecture::Supports(uint32_t DeviceCapability) const noexcept
	{
		if (!mComputeCapability)
		{
			return false;
		}
		if (mbExact || mComputeCapability >= 100)
		{
			return mComputeCapability == DeviceCapability;
		}
		return mComputeCapability / 10 == DeviceCapability / 10 && mComputeCapability <= DeviceCapability;
	}

	FArdaRHIStatus ValidateArdaCudaBuffer(const FArdaRHIBufferDesc& D)
	{
		if (!D.mByteSize || D.mbVirtual || D.mbTiled || D.mMaxVersions || D.mCpuAccess != EArdaRHICpuAccess::None ||
		    HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::AccelStructStorage))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "CUDA requires a nonempty dedicated device-local buffer; AS storage, versioned, placed and sparse buffers are unsupported.");
		}
		return {};
	}

	FArdaRHIStatus ValidateArdaCudaTexture(const FArdaRHITextureDesc& D)
	{
		if (D.mbVirtual || D.mbTiled || D.mSampleCount != 1 ||
		    (D.mDimension != EArdaRHITextureDimension::Texture1D &&
		        D.mDimension != EArdaRHITextureDimension::Texture1DArray &&
		        D.mDimension != EArdaRHITextureDimension::Texture2D &&
		        D.mDimension != EArdaRHITextureDimension::Texture2DArray &&
		        D.mDimension != EArdaRHITextureDimension::Texture3D) ||
		    HasAnyFlags(D.mUsage, EArdaRHITextureUsage::DepthStencil))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "CUDA surfaces require dedicated, non-MSAA, non-depth, non-cube textures.");
		}
		if (GetArdaCudaFormatInfo(D.mFormat).mChannels)
		{
			return {};
		}
		return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
		    "CUDA surfaces require uncompressed integer or floating-point R/RG/RGBA formats; color conversion is explicit.");
	}

	FArdaCudaFormatInfo GetArdaCudaFormatInfo(EArdaRHIFormat Format) noexcept
	{
		// This is the sole channel-layout table used by admission and native imports.
		// Normalized/packed/color formats need an explicit conversion operator.
		switch (Format)
		{
		case EArdaRHIFormat::R8UInt:
			return {EArdaCudaScalarType::UInt, 8, 1};
		case EArdaRHIFormat::R8SInt:
			return {EArdaCudaScalarType::SInt, 8, 1};
		case EArdaRHIFormat::RG8UInt:
			return {EArdaCudaScalarType::UInt, 8, 2};
		case EArdaRHIFormat::RG8SInt:
			return {EArdaCudaScalarType::SInt, 8, 2};
		case EArdaRHIFormat::RGBA8UInt:
			return {EArdaCudaScalarType::UInt, 8, 4};
		case EArdaRHIFormat::RGBA8SInt:
			return {EArdaCudaScalarType::SInt, 8, 4};
		case EArdaRHIFormat::R16UInt:
			return {EArdaCudaScalarType::UInt, 16, 1};
		case EArdaRHIFormat::R16SInt:
			return {EArdaCudaScalarType::SInt, 16, 1};
		case EArdaRHIFormat::R16Float:
			return {EArdaCudaScalarType::Float, 16, 1};
		case EArdaRHIFormat::RG16UInt:
			return {EArdaCudaScalarType::UInt, 16, 2};
		case EArdaRHIFormat::RG16SInt:
			return {EArdaCudaScalarType::SInt, 16, 2};
		case EArdaRHIFormat::RG16Float:
			return {EArdaCudaScalarType::Float, 16, 2};
		case EArdaRHIFormat::RGBA16UInt:
			return {EArdaCudaScalarType::UInt, 16, 4};
		case EArdaRHIFormat::RGBA16SInt:
			return {EArdaCudaScalarType::SInt, 16, 4};
		case EArdaRHIFormat::RGBA16Float:
			return {EArdaCudaScalarType::Float, 16, 4};
		case EArdaRHIFormat::R32UInt:
			return {EArdaCudaScalarType::UInt, 32, 1};
		case EArdaRHIFormat::R32SInt:
			return {EArdaCudaScalarType::SInt, 32, 1};
		case EArdaRHIFormat::R32Float:
			return {EArdaCudaScalarType::Float, 32, 1};
		case EArdaRHIFormat::RG32UInt:
			return {EArdaCudaScalarType::UInt, 32, 2};
		case EArdaRHIFormat::RG32SInt:
			return {EArdaCudaScalarType::SInt, 32, 2};
		case EArdaRHIFormat::RG32Float:
			return {EArdaCudaScalarType::Float, 32, 2};
		case EArdaRHIFormat::RGBA32UInt:
			return {EArdaCudaScalarType::UInt, 32, 4};
		case EArdaRHIFormat::RGBA32SInt:
			return {EArdaCudaScalarType::SInt, 32, 4};
		case EArdaRHIFormat::RGBA32Float:
			return {EArdaCudaScalarType::Float, 32, 4};
		default:
			return {};
		}
	}

	FArdaRHIStatus IArdaCudaExternalCall::GetSupport(const FArdaCudaCapabilities& Capabilities) const
	{
		if (Capabilities.mLaunchMode != EArdaCudaLaunchMode::ContextSwitch &&
		    Capabilities.mLaunchMode != EArdaCudaLaunchMode::VulkanCiG)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "External CUDA call has not qualified this execution mode; D3D12 CiG requires explicit adapter support.");
		}
		return {};
	}

	TArdaRHIResult<eastl::unique_ptr<IArdaCudaExternalCallState>> IArdaCudaExternalCall::CreateContextState(
	    const FArdaCudaExternalCallContext&) const
	{
		return {};
	}

	FArdaRHIStatus IArdaCudaExternalCall::CheckSupport(const FArdaCudaCapabilities& Capabilities) const
	{
		try
		{
			return GetSupport(Capabilities);
		}
		catch (const std::exception& Error)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Error.what());
		}
		catch (...)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "External CUDA support check threw an exception.");
		}
	}

	FArdaRHIStatus ValidateArdaCudaKernels(const eastl::vector<FArdaCudaKernel>& Kernels,
	    size_t BindingCount,
	    const FArdaCudaCapabilities& C)
	{
		if (!C)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, C.mUnavailableReason.c_str());
		}
		if (Kernels.size() != 1)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Each CUDA dispatch must contain exactly one kernel or external call.");
		}
		return ValidateArdaCudaKernelBatch(Kernels, BindingCount, C);
	}

	static FArdaRHIStatus ValidateCudaKernel(const FArdaCudaKernel& K,
	    size_t BindingCount,
	    const FArdaCudaCapabilities& C)
	{
		if (bool(K.mEntry) == bool(K.mExternalCall))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "CUDA operation requires exactly one compiled entry or external call.");
		}
		const auto Signature = K.mEntry ? K.mEntry->GetSignature() : K.mExternalCall->GetSignature();
		if (!Signature.mbSupported || !Signature.mType || Signature.mSize != K.mParameters.size() || !Signature.mSize ||
		    !Signature.mAlignment || (Signature.mAlignment & (Signature.mAlignment - 1)))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "CUDA parameter signature or size is invalid.");
		}
		if (K.mExternalCall)
		{
			if (auto Status = K.mExternalCall->CheckSupport(C); !Status)
			{
				return Status;
			}
		}
		else
		{
			bool Supported = false;
			for (const auto& A : K.mEntry->GetBuildInfo().mArchitectures)
			{
				if (A.Supports(C.mComputeCapability))
				{
					Supported = true;
					break;
				}
			}
			if (!Supported)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    "No precompiled native kernel image supports this CUDA architecture.");
			}
			if (K.mSharedMemoryBytes > C.mMaxSharedMemoryBytes)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "CUDA shared-memory requirement is invalid.");
			}
			uint64_t Threads = 1;
			for (uint32_t Axis = 0; Axis < 3; ++Axis)
			{
				if (!K.mGridSize[Axis] || K.mGridSize[Axis] > C.mMaxGridSize[Axis] || !K.mBlockSize[Axis] ||
				    K.mBlockSize[Axis] > C.mMaxBlockSize[Axis])
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "CUDA launch dimensions exceed the device limits.");
				}

				// Check before multiplying, including capabilities supplied by custom providers.
				if (Threads > C.mMaxThreadsPerBlock / K.mBlockSize[Axis])
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA block has too many threads.");
				}
				Threads *= K.mBlockSize[Axis];
			}
		}
		for (size_t I = 0; I < K.mPatches.size(); ++I)
		{
			const auto& P = K.mPatches[I];
			if (P.mBindingIndex >= BindingCount || P.mOffset > K.mParameters.size() ||
			    sizeof(uint64_t) > K.mParameters.size() - P.mOffset || !P.mAlignment ||
			    (P.mAlignment & (P.mAlignment - 1)))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "CUDA parameter resource patch is outside the argument or has invalid alignment.");
			}
			for (size_t J = 0; J < I; ++J)
			{
				if (P.mOffset < K.mPatches[J].mOffset + sizeof(uint64_t) &&
				    K.mPatches[J].mOffset < P.mOffset + sizeof(uint64_t))
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA resource patches overlap.");
				}
			}
		}
		return {};
	}

	FArdaRHIStatus ValidateArdaCudaKernelBatch(const eastl::vector<FArdaCudaKernel>& Kernels,
	    size_t BindingCount,
	    const FArdaCudaCapabilities& C)
	{
		if (!C)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, C.mUnavailableReason.c_str());
		}
		if (Kernels.empty())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "CUDA batch requires at least one operation.");
		}
		for (const auto& Kernel : Kernels)
		{
			if (auto Status = ValidateCudaKernel(Kernel, BindingCount, C); !Status)
			{
				return Status;
			}
		}
		return {};
	}
}
