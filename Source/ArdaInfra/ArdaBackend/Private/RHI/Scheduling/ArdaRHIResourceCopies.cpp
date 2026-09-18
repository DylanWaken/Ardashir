#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Scheduling/ArdaRHIResourceCopies.h"

#include "RHI/Resources/ArdaHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{
	FArdaRHIStatus ResolveArdaRHITextureCopyExtent(const FArdaRHITextureDesc& DestinationDesc,
	    const FArdaRHITextureSlice& DestinationSlice,
	    const FArdaRHITextureDesc& SourceDesc,
	    const FArdaRHITextureSlice& SourceSlice,
	    FArdaRHITextureCopyExtent& OutExtent) noexcept
	{
		OutExtent = {};
		const auto Invalid = [](const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		};
		if (!IsArdaRHIFormatKnown(SourceDesc.mFormat) || DestinationDesc.mFormat != SourceDesc.mFormat)
		{
			return Invalid("Texture copy formats must match.");
		}
		const uint32_t PlaneCount = GetArdaRHIFormatPlaneCount(SourceDesc.mFormat);
		if (DestinationSlice.mMipLevel >= DestinationDesc.mMipLevels ||
		    SourceSlice.mMipLevel >= SourceDesc.mMipLevels ||
		    DestinationSlice.mArraySlice >= DestinationDesc.mArraySize ||
		    SourceSlice.mArraySlice >= SourceDesc.mArraySize || DestinationSlice.mPlane >= PlaneCount ||
		    SourceSlice.mPlane >= PlaneCount || DestinationSlice.mPlane != SourceSlice.mPlane)
		{
			return Invalid("Texture copy subresources are incompatible.");
		}

		const uint32_t SourceWidth = GetArdaRHITextureMipExtent(SourceDesc.mWidth, SourceSlice.mMipLevel);
		const uint32_t SourceHeight = GetArdaRHITextureMipExtent(SourceDesc.mHeight, SourceSlice.mMipLevel);
		const uint32_t SourceDepth = GetArdaRHITextureMipExtent(SourceDesc.mDepth, SourceSlice.mMipLevel);
		if (SourceSlice.mX >= SourceWidth || SourceSlice.mY >= SourceHeight || SourceSlice.mZ >= SourceDepth)
		{
			return Invalid("Texture copy source origin is out of range.");
		}

		OutExtent.mWidth = eastl::min(SourceSlice.mWidth, SourceWidth - SourceSlice.mX);
		OutExtent.mHeight = eastl::min(SourceSlice.mHeight, SourceHeight - SourceSlice.mY);
		OutExtent.mDepth = eastl::min(SourceSlice.mDepth, SourceDepth - SourceSlice.mZ);
		const uint32_t DestinationWidth =
		    GetArdaRHITextureMipExtent(DestinationDesc.mWidth, DestinationSlice.mMipLevel);
		const uint32_t DestinationHeight =
		    GetArdaRHITextureMipExtent(DestinationDesc.mHeight, DestinationSlice.mMipLevel);
		const uint32_t DestinationDepth =
		    GetArdaRHITextureMipExtent(DestinationDesc.mDepth, DestinationSlice.mMipLevel);
		const FArdaRHIFormatInfo& FormatInfo = GetArdaRHIFormatInfo(SourceDesc.mFormat);
		if (SourceSlice.mX % FormatInfo.mBlockWidth || DestinationSlice.mX % FormatInfo.mBlockWidth ||
		    SourceSlice.mY % FormatInfo.mBlockHeight || DestinationSlice.mY % FormatInfo.mBlockHeight)
		{
			OutExtent = {};
			return Invalid("Texture copy origins must be aligned to format blocks.");
		}
		const bool bWidthEndsAtBothEdges = OutExtent.mWidth == SourceWidth - SourceSlice.mX &&
		    DestinationSlice.mX <= DestinationWidth && OutExtent.mWidth == DestinationWidth - DestinationSlice.mX;
		const bool bHeightEndsAtBothEdges = OutExtent.mHeight == SourceHeight - SourceSlice.mY &&
		    DestinationSlice.mY <= DestinationHeight && OutExtent.mHeight == DestinationHeight - DestinationSlice.mY;
		if ((OutExtent.mWidth % FormatInfo.mBlockWidth && !bWidthEndsAtBothEdges) ||
		    (OutExtent.mHeight % FormatInfo.mBlockHeight && !bHeightEndsAtBothEdges))
		{
			OutExtent = {};
			return Invalid("Texture copy extents must be block-aligned unless both regions reach an edge.");
		}
		if (!OutExtent.mWidth || !OutExtent.mHeight || !OutExtent.mDepth || DestinationSlice.mX > DestinationWidth ||
		    OutExtent.mWidth > DestinationWidth - DestinationSlice.mX || DestinationSlice.mY > DestinationHeight ||
		    OutExtent.mHeight > DestinationHeight - DestinationSlice.mY || DestinationSlice.mZ > DestinationDepth ||
		    OutExtent.mDepth > DestinationDepth - DestinationSlice.mZ)
		{
			OutExtent = {};
			return Invalid("Texture copy destination region is out of range.");
		}
		return {};
	}

	FArdaRHIStatus ValidateArdaRHITextureBufferCopy(const FArdaRHITextureDesc& TextureDesc,
	    const FArdaRHITextureSlice& Slice,
	    const FArdaRHIBufferDesc& BufferDesc,
	    const FArdaRHITextureBufferLayout& Layout,
	    FArdaRHITextureCopyExtent& OutExtent) noexcept
	{
		OutExtent = {};
		const auto Invalid = [](const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		};
		if (auto Status = Validate(TextureDesc); !Status)
		{
			return Status;
		}
		const auto Dimension = TextureDesc.mDimension;
		if ((Dimension != EArdaRHITextureDimension::Texture3D && TextureDesc.mDepth != 1) ||
		    (Dimension == EArdaRHITextureDimension::Texture3D && TextureDesc.mArraySize != 1) ||
		    ((Dimension == EArdaRHITextureDimension::Texture1D ||
		         Dimension == EArdaRHITextureDimension::Texture1DArray) &&
		        TextureDesc.mHeight != 1) ||
		    Dimension == EArdaRHITextureDimension::Texture2DMS ||
		    Dimension == EArdaRHITextureDimension::Texture2DMSArray ||
		    static_cast<uint8_t>(Dimension) > static_cast<uint8_t>(EArdaRHITextureDimension::Texture3D))
		{
			return Invalid("Texture-buffer copy dimensions are inconsistent or multisampled.");
		}
		const auto& Format = GetArdaRHIFormatInfo(TextureDesc.mFormat);
		if (!IsArdaRHIFormatKnown(TextureDesc.mFormat) || !Format.mBytesPerBlock || Format.mbDepth ||
		    Format.mbStencil || Format.mBlockWidth != 1 || Format.mBlockHeight != 1 || TextureDesc.mSampleCount != 1 ||
		    HasAnyFlags(TextureDesc.mUsage, EArdaRHITextureUsage::Typeless))
		{
			return Invalid("Texture-buffer copies require a typed, single-sample, uncompressed color format.");
		}
		if (HasAnyFlags(BufferDesc.mUsage, EArdaRHIBufferUsage::AccelStructStorage))
		{
			return Invalid("An acceleration-structure storage buffer cannot be used for texture copies.");
		}
		if (!Layout.mRowPitch || Layout.mRowPitch > INT32_MAX || Layout.mRowPitch % 256 || Layout.mByteOffset % 512 ||
		    Layout.mRowPitch % Format.mBytesPerBlock || Layout.mByteOffset % Format.mBytesPerBlock)
		{
			return Invalid(
			    "Texture-buffer pitch must fit INT32_MAX; pitch/offset must align to 256/512 bytes and whole texels.");
		}
		FArdaRHITextureCopyExtent Extent;
		if (auto Status = ResolveArdaRHITextureCopyExtent(TextureDesc, Slice, TextureDesc, Slice, Extent); !Status)
		{
			return Status;
		}
		if ((Slice.mWidth != ArdaRHIAllSubresources && Slice.mWidth != Extent.mWidth) ||
		    (Slice.mHeight != ArdaRHIAllSubresources && Slice.mHeight != Extent.mHeight) ||
		    (Slice.mDepth != ArdaRHIAllSubresources && Slice.mDepth != Extent.mDepth))
		{
			return Invalid("Texture-buffer copy extent exceeds the selected mip region.");
		}
		const uint64_t RowBytes = uint64_t(Extent.mWidth) * Format.mBytesPerBlock;
		const uint64_t Rows = uint64_t(Extent.mHeight) * Extent.mDepth;
		if (RowBytes > Layout.mRowPitch || Layout.mByteOffset > BufferDesc.mByteSize)
		{
			return Invalid("Texture-buffer row pitch or byte offset is out of range.");
		}
		const uint64_t Available = BufferDesc.mByteSize - Layout.mByteOffset;
		// Division bounds the final addressed row without overflowing row/slice-pitch products.
		if (RowBytes > Available || Rows - 1 > (Available - RowBytes) / Layout.mRowPitch)
		{
			return Invalid("Texture-buffer copy exceeds the buffer allocation.");
		}
		OutExtent = Extent;
		return {};
	}

	TArdaRHIResult<FArdaRHITextureBufferFootprint> GetArdaRHITextureBufferFootprint(
	    const FArdaRHITextureDesc& TextureDesc,
	    const FArdaRHITextureSlice& Slice) noexcept
	{
		const auto Invalid = [](const char* Message)
		{
			return TArdaRHIResult<FArdaRHITextureBufferFootprint>{{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message)};
		};
		if (auto Status = Validate(TextureDesc); !Status)
		{
			return {{}, Status};
		}
		const uint32_t ElementSize = GetArdaRHIFormatElementSize(TextureDesc.mFormat);
		if (!ElementSize)
		{
			return Invalid("Texture-buffer copies require a typed, single-sample, uncompressed color format.");
		}
		FArdaRHITextureBufferFootprint Footprint;
		if (auto Status = ResolveArdaRHITextureCopyExtent(TextureDesc, Slice, TextureDesc, Slice, Footprint.mExtent);
		    !Status)
		{
			return {{}, Status};
		}
		uint32_t Divisor = 256;
		for (uint32_t Remainder = ElementSize; Remainder;)
		{
			const uint32_t Next = Divisor % Remainder;
			Divisor = Remainder;
			Remainder = Next;
		}
		const uint64_t Alignment = uint64_t(256 / Divisor) * ElementSize;
		Footprint.mRowBytes = uint64_t(Footprint.mExtent.mWidth) * ElementSize;
		Footprint.mRowCount = uint64_t(Footprint.mExtent.mHeight) * Footprint.mExtent.mDepth;
		const uint64_t RowPitch = ((Footprint.mRowBytes - 1) / Alignment + 1) * Alignment;
		if (RowPitch > INT32_MAX)
		{
			return Invalid("Texture-buffer row pitch exceeds the portable INT32_MAX limit.");
		}
		Footprint.mLayout.mRowPitch = static_cast<uint32_t>(RowPitch);
		if (Footprint.mRowCount - 1 > (UINT64_MAX - Footprint.mRowBytes) / RowPitch)
		{
			return Invalid("Texture-buffer footprint size overflows uint64_t.");
		}
		Footprint.mByteSize = (Footprint.mRowCount - 1) * RowPitch + Footprint.mRowBytes;
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = Footprint.mByteSize;
		if (auto Status =
		        ValidateArdaRHITextureBufferCopy(TextureDesc, Slice, BufferDesc, Footprint.mLayout, Footprint.mExtent);
		    !Status)
		{
			return {{}, Status};
		}
		return {Footprint, {}};
	}

	FArdaRHIStatus ValidateArdaRHITextureResolve(const FArdaRHITextureDesc& DestinationDesc,
	    const FArdaRHITextureSlice& DestinationSlice,
	    const FArdaRHITextureDesc& SourceDesc,
	    const FArdaRHITextureSlice& SourceSlice,
	    FArdaRHITextureCopyExtent& OutExtent) noexcept
	{
		OutExtent = {};
		const auto Invalid = [](const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		};
		if (SourceDesc.mSampleCount <= 1 || DestinationDesc.mSampleCount != 1)
		{
			return Invalid("Texture resolve requires a multisample source and single-sample destination.");
		}
		if (!IsArdaRHIFormatKnown(SourceDesc.mFormat) || DestinationDesc.mFormat != SourceDesc.mFormat ||
		    GetArdaRHIFormatInfo(SourceDesc.mFormat).mbDepth)
		{
			return Invalid("Texture resolve requires matching color formats.");
		}
		if (DestinationSlice.mX || DestinationSlice.mY || DestinationSlice.mZ || SourceSlice.mX || SourceSlice.mY ||
		    SourceSlice.mZ || DestinationSlice.mPlane || SourceSlice.mPlane ||
		    DestinationSlice.mMipLevel >= DestinationDesc.mMipLevels ||
		    SourceSlice.mMipLevel >= SourceDesc.mMipLevels ||
		    DestinationSlice.mArraySlice >= DestinationDesc.mArraySize ||
		    SourceSlice.mArraySlice >= SourceDesc.mArraySize)
		{
			return Invalid("Texture resolve requires valid whole color subresources.");
		}

		OutExtent = {GetArdaRHITextureMipExtent(SourceDesc.mWidth, SourceSlice.mMipLevel),
		    GetArdaRHITextureMipExtent(SourceDesc.mHeight, SourceSlice.mMipLevel),
		    GetArdaRHITextureMipExtent(SourceDesc.mDepth, SourceSlice.mMipLevel)};
		if (OutExtent.mWidth != GetArdaRHITextureMipExtent(DestinationDesc.mWidth, DestinationSlice.mMipLevel) ||
		    OutExtent.mHeight != GetArdaRHITextureMipExtent(DestinationDesc.mHeight, DestinationSlice.mMipLevel) ||
		    OutExtent.mDepth != GetArdaRHITextureMipExtent(DestinationDesc.mDepth, DestinationSlice.mMipLevel))
		{
			OutExtent = {};
			return Invalid("Texture resolve source and destination subresource extents must match.");
		}
		return {};
	}
}
