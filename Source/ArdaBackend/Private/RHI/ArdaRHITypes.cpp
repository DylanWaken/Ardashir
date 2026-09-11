#include "RHI/ArdaRHITypes.h"

#include "ArdaHash.h"

#include <EASTL/algorithm.h>

namespace arda
{
	FArdaRHITextureSubresourceRange FArdaRHITextureSubresourceRange::Resolve(
	    const FArdaRHITextureDesc& Desc) const noexcept
	{
		FArdaRHITextureSubresourceRange Result = *this;
		Result.mMipLevelCount =
		    eastl::min(Result.mMipLevelCount, Desc.mMipLevels - eastl::min(Result.mBaseMipLevel, Desc.mMipLevels));
		Result.mArraySliceCount =
		    eastl::min(Result.mArraySliceCount, Desc.mArraySize - eastl::min(Result.mBaseArraySlice, Desc.mArraySize));
		const uint32_t PlaneCount = GetArdaRHIFormatPlaneCount(Desc.mFormat);
		Result.mPlaneCount = eastl::min(Result.mPlaneCount, PlaneCount - eastl::min(Result.mBasePlane, PlaneCount));
		return Result;
	}

	FArdaRHIBufferRange FArdaRHIBufferRange::Resolve(const FArdaRHIBufferDesc& Desc) const noexcept
	{
		FArdaRHIBufferRange Result = *this;
		Result.mByteOffset = eastl::min(Result.mByteOffset, Desc.mByteSize);
		Result.mByteSize = eastl::min(Result.mByteSize, Desc.mByteSize - Result.mByteOffset);
		return Result;
	}

	bool FArdaRHIBufferRange::IsWholeBuffer(const FArdaRHIBufferDesc& Desc) const noexcept
	{
		const auto Result = Resolve(Desc);
		return Result.mByteOffset == 0 && Result.mByteSize == Desc.mByteSize;
	}

	const FArdaRHIFormatInfo& GetArdaRHIFormatInfo(EArdaRHIFormat Format) noexcept
	{
		static const FArdaRHIFormatInfo Default{};
		static const FArdaRHIFormatInfo Normal1{false, false, false, 1, 1, 1};
		static const FArdaRHIFormatInfo Normal2{false, false, false, 2, 1, 1};
		static const FArdaRHIFormatInfo Normal4{false, false, false, 4, 1, 1};
		static const FArdaRHIFormatInfo Normal8{false, false, false, 8, 1, 1};
		static const FArdaRHIFormatInfo Normal12{false, false, false, 12, 1, 1};
		static const FArdaRHIFormatInfo Normal16{false, false, false, 16, 1, 1};
		static const FArdaRHIFormatInfo Integer1{false, false, true, 1, 1, 1};
		static const FArdaRHIFormatInfo Integer2{false, false, true, 2, 1, 1};
		static const FArdaRHIFormatInfo Integer4{false, false, true, 4, 1, 1};
		static const FArdaRHIFormatInfo Integer8{false, false, true, 8, 1, 1};
		static const FArdaRHIFormatInfo Integer12{false, false, true, 12, 1, 1};
		static const FArdaRHIFormatInfo Integer16{false, false, true, 16, 1, 1};
		static const FArdaRHIFormatInfo Depth2{true, false, false, 2, 1, 1};
		static const FArdaRHIFormatInfo Depth4{true, false, false, 4, 1, 1};
		static const FArdaRHIFormatInfo DepthStencil4{true, true, false, 4, 1, 1};
		static const FArdaRHIFormatInfo DepthStencil8{true, true, false, 8, 1, 1};
		static const FArdaRHIFormatInfo Block8{false, false, false, 8, 4, 4};
		static const FArdaRHIFormatInfo Block16{false, false, false, 16, 4, 4};
		switch (Format)
		{
		case EArdaRHIFormat::R8UInt:
		case EArdaRHIFormat::R8SInt:
			return Integer1;
		case EArdaRHIFormat::RG8UInt:
		case EArdaRHIFormat::RG8SInt:
		case EArdaRHIFormat::R16UInt:
		case EArdaRHIFormat::R16SInt:
			return Integer2;
		case EArdaRHIFormat::RGBA8UInt:
		case EArdaRHIFormat::RGBA8SInt:
		case EArdaRHIFormat::RG16UInt:
		case EArdaRHIFormat::RG16SInt:
		case EArdaRHIFormat::R32UInt:
		case EArdaRHIFormat::R32SInt:
			return Integer4;
		case EArdaRHIFormat::RGBA16UInt:
		case EArdaRHIFormat::RGBA16SInt:
		case EArdaRHIFormat::RG32UInt:
		case EArdaRHIFormat::RG32SInt:
			return Integer8;
		case EArdaRHIFormat::RGB32UInt:
		case EArdaRHIFormat::RGB32SInt:
			return Integer12;
		case EArdaRHIFormat::RGBA32UInt:
		case EArdaRHIFormat::RGBA32SInt:
			return Integer16;
		case EArdaRHIFormat::R8UNorm:
		case EArdaRHIFormat::R8SNorm:
			return Normal1;
		case EArdaRHIFormat::RG8UNorm:
		case EArdaRHIFormat::RG8SNorm:
		case EArdaRHIFormat::R16UNorm:
		case EArdaRHIFormat::R16SNorm:
		case EArdaRHIFormat::R16Float:
			return Normal2;
		case EArdaRHIFormat::RGBA8UNorm:
		case EArdaRHIFormat::RGBA8SNorm:
		case EArdaRHIFormat::BGRA8UNorm:
		case EArdaRHIFormat::SRGBA8UNorm:
		case EArdaRHIFormat::SBGRA8UNorm:
		case EArdaRHIFormat::R10G10B10A2UNorm:
		case EArdaRHIFormat::R11G11B10Float:
		case EArdaRHIFormat::RG16UNorm:
		case EArdaRHIFormat::RG16SNorm:
		case EArdaRHIFormat::RG16Float:
		case EArdaRHIFormat::R32Float:
			return Normal4;
		case EArdaRHIFormat::RGBA16Float:
		case EArdaRHIFormat::RGBA16UNorm:
		case EArdaRHIFormat::RGBA16SNorm:
		case EArdaRHIFormat::RG32Float:
			return Normal8;
		case EArdaRHIFormat::RGB32Float:
			return Normal12;
		case EArdaRHIFormat::RGBA32Float:
			return Normal16;
		case EArdaRHIFormat::D16:
			return Depth2;
		case EArdaRHIFormat::D24S8:
			return DepthStencil4;
		case EArdaRHIFormat::D32:
			return Depth4;
		case EArdaRHIFormat::D32S8:
			return DepthStencil8;
		case EArdaRHIFormat::BC1UNorm:
		case EArdaRHIFormat::BC1UNormSRGB:
		case EArdaRHIFormat::BC4UNorm:
		case EArdaRHIFormat::BC4SNorm:
			return Block8;
		case EArdaRHIFormat::BC2UNorm:
		case EArdaRHIFormat::BC2UNormSRGB:
		case EArdaRHIFormat::BC3UNorm:
		case EArdaRHIFormat::BC3UNormSRGB:
		case EArdaRHIFormat::BC5UNorm:
		case EArdaRHIFormat::BC5SNorm:
		case EArdaRHIFormat::BC6HUFloat:
		case EArdaRHIFormat::BC6HSFloat:
		case EArdaRHIFormat::BC7UNorm:
		case EArdaRHIFormat::BC7UNormSRGB:
			return Block16;
		default:
			return Default;
		}
	}

	uint32_t GetArdaRHIFormatElementSize(EArdaRHIFormat Format) noexcept
	{
		const FArdaRHIFormatInfo& Info = GetArdaRHIFormatInfo(Format);
		return Info.mBlockWidth == 1 && Info.mBlockHeight == 1 ? Info.mBytesPerBlock : 0;
	}

	uint32_t GetArdaRHIFormatPlaneCount(EArdaRHIFormat Format) noexcept
	{
		return GetArdaRHIFormatInfo(Format).mbStencil ? 2u : 1u;
	}

	uint32_t GetArdaRHITextureMipExtent(uint32_t BaseExtent, uint32_t MipLevel) noexcept
	{
		return MipLevel >= 32 ? 1u : eastl::max(1u, BaseExtent >> MipLevel);
	}

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

	bool FArdaRHITextureDesc::operator==(const FArdaRHITextureDesc& O) const noexcept
	{
		return mbCudaInterop == O.mbCudaInterop && mWidth == O.mWidth && mHeight == O.mHeight && mDepth == O.mDepth &&
		    mArraySize == O.mArraySize && mMipLevels == O.mMipLevels && mSampleCount == O.mSampleCount &&
		    mFormat == O.mFormat && mDimension == O.mDimension && mUsage == O.mUsage &&
		    mInitialState == O.mInitialState && mbKeepInitialState == O.mbKeepInitialState &&
		    mbVirtual == O.mbVirtual && mbTiled == O.mbTiled && mClearValue == O.mClearValue &&
		    mbUseClearValue == O.mbUseClearValue && mDebugName == O.mDebugName;
	}

	bool FArdaRHIBufferDesc::operator==(const FArdaRHIBufferDesc& O) const noexcept
	{
		return mbCudaInterop == O.mbCudaInterop && mByteSize == O.mByteSize && mStructureStride == O.mStructureStride &&
		    mMaxVersions == O.mMaxVersions && mFormat == O.mFormat && mUsage == O.mUsage &&
		    mCpuAccess == O.mCpuAccess && mInitialState == O.mInitialState &&
		    mbKeepInitialState == O.mbKeepInitialState && mbVirtual == O.mbVirtual && mbTiled == O.mbTiled &&
		    mDebugName == O.mDebugName;
	}

	bool FArdaRHISamplerDesc::operator==(const FArdaRHISamplerDesc& O) const noexcept
	{
		return mBorderColor == O.mBorderColor && mMaxAnisotropy == O.mMaxAnisotropy && mMipBias == O.mMipBias &&
		    mbMinFilter == O.mbMinFilter && mbMagFilter == O.mbMagFilter && mbMipFilter == O.mbMipFilter &&
		    mAddressU == O.mAddressU && mAddressV == O.mAddressV && mAddressW == O.mAddressW &&
		    mReduction == O.mReduction;
	}

	size_t HashValue(const FArdaRHITextureSubresourceRange& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, V.mBaseMipLevel);
		ArdaHashCombine(H, V.mMipLevelCount);
		ArdaHashCombine(H, V.mBaseArraySlice);
		ArdaHashCombine(H, V.mArraySliceCount);
		ArdaHashCombine(H, V.mBasePlane);
		ArdaHashCombine(H, V.mPlaneCount);
		return H;
	}

	size_t HashValue(const FArdaRHIBufferRange& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, V.mByteOffset);
		ArdaHashCombine(H, V.mByteSize);
		return H;
	}

	size_t HashValue(const FArdaRHITextureDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, V.mWidth);
		ArdaHashCombine(H, V.mHeight);
		ArdaHashCombine(H, V.mDepth);
		ArdaHashCombine(H, V.mArraySize);
		ArdaHashCombine(H, V.mMipLevels);
		ArdaHashCombine(H, V.mSampleCount);
		ArdaHashCombine(H, static_cast<uint8_t>(V.mFormat));
		ArdaHashCombine(H, static_cast<uint8_t>(V.mDimension));
		ArdaHashCombine(H, static_cast<uint16_t>(V.mUsage));
		ArdaHashCombine(H, static_cast<uint32_t>(V.mInitialState));
		ArdaHashCombine(H, V.mbKeepInitialState);
		ArdaHashCombine(H, V.mbVirtual);
		ArdaHashCombine(H, V.mbTiled);
		ArdaHashCombine(H, ArdaFloatBits(V.mClearValue.mR));
		ArdaHashCombine(H, ArdaFloatBits(V.mClearValue.mG));
		ArdaHashCombine(H, ArdaFloatBits(V.mClearValue.mB));
		ArdaHashCombine(H, ArdaFloatBits(V.mClearValue.mA));
		ArdaHashCombine(H, V.mbUseClearValue);
		ArdaHashString(H, V.mDebugName);
		return H;
	}

	size_t HashValue(const FArdaRHIBufferDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, V.mByteSize);
		ArdaHashCombine(H, V.mStructureStride);
		ArdaHashCombine(H, V.mMaxVersions);
		ArdaHashCombine(H, static_cast<uint8_t>(V.mFormat));
		ArdaHashCombine(H, static_cast<uint16_t>(V.mUsage));
		ArdaHashCombine(H, static_cast<uint8_t>(V.mCpuAccess));
		ArdaHashCombine(H, static_cast<uint32_t>(V.mInitialState));
		ArdaHashCombine(H, V.mbKeepInitialState);
		ArdaHashCombine(H, V.mbVirtual);
		ArdaHashCombine(H, V.mbTiled);
		ArdaHashString(H, V.mDebugName);
		return H;
	}

	size_t HashValue(const FArdaRHISamplerDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, ArdaFloatBits(V.mBorderColor.mR));
		ArdaHashCombine(H, ArdaFloatBits(V.mBorderColor.mG));
		ArdaHashCombine(H, ArdaFloatBits(V.mBorderColor.mB));
		ArdaHashCombine(H, ArdaFloatBits(V.mBorderColor.mA));
		ArdaHashCombine(H, ArdaFloatBits(V.mMaxAnisotropy));
		ArdaHashCombine(H, ArdaFloatBits(V.mMipBias));
		ArdaHashCombine(H, V.mbMinFilter);
		ArdaHashCombine(H, V.mbMagFilter);
		ArdaHashCombine(H, V.mbMipFilter);
		ArdaHashCombine(H, static_cast<uint8_t>(V.mAddressU));
		ArdaHashCombine(H, static_cast<uint8_t>(V.mAddressV));
		ArdaHashCombine(H, static_cast<uint8_t>(V.mAddressW));
		ArdaHashCombine(H, static_cast<uint8_t>(V.mReduction));
		return H;
	}
}
