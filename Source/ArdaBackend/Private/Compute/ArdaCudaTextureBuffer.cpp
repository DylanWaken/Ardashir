#include "Compute/ArdaCudaTextureBuffer.h"
#include <limits>

namespace arda
{
	TArdaRHIResult<FArdaCudaTextureBuffer> CreateArdaCudaTextureBuffer(IArdaRHIDevice& Device,
	    const FArdaRHITextureDesc& Texture,
	    const FArdaRHITextureSlice& Slice)
	{
		FArdaCudaTextureBuffer Result;
		if (auto Status = Validate(Texture); !Status)
		{
			return {{}, Status};
		}
		if (auto Status = ResolveArdaRHITextureCopyExtent(Texture, Slice, Texture, Slice, Result.mExtent); !Status)
		{
			return {{}, Status};
		}
		const auto Format = GetArdaCudaFormatInfo(Texture.mFormat);
		if (!Format.mChannels)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "CUDA texture buffers require an uncompressed CUDA-compatible color format.")};
		}
		const uint64_t RowBytes = uint64_t(Result.mExtent.mWidth) * Format.mChannels * Format.mBits / 8;
		const uint64_t RowPitch = (RowBytes + 255) & ~uint64_t(255);
		if (RowPitch > std::numeric_limits<uint32_t>::max())
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA texture row pitch overflows.")};
		}
		Result.mLayout.mRowPitch = static_cast<uint32_t>(RowPitch);
		Result.mSlicePitch = RowPitch * Result.mExtent.mHeight;
		if (Result.mSlicePitch > std::numeric_limits<uint64_t>::max() / Result.mExtent.mDepth)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA texture buffer size overflows.")};
		}
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = Result.mSlicePitch * Result.mExtent.mDepth;
		Buffer.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
		Buffer.mbCudaInterop = true;
		Buffer.mDebugName = Texture.mDebugName + ".cuda-buffer";
		if (auto Status = ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Result.mLayout, Result.mExtent);
		    !Status)
		{
			return {{}, Status};
		}
		auto Storage = Device.CreateBuffer(Buffer);
		if (!Storage)
		{
			return {{}, Storage.mStatus};
		}
		Result.mBuffer = eastl::move(Storage.mValue);
		return {eastl::move(Result), {}};
	}
}
