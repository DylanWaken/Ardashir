#include "RHI/Config/ArdaRHICapabilities.h"
#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Scheduling/ArdaRHIResourceStates.h"

#include "RHI/Resources/ArdaHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{
	namespace
	{
		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		template <typename Descriptor, typename Usage>
		bool AllowsInitialState(const Descriptor& Desc, EArdaRHIResourceState State, Usage RequiredUsage)
		{
			return !HasAnyFlags(Desc.mInitialState, State) || HasAnyFlags(Desc.mUsage, RequiredUsage);
		}
	}

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

	bool IsArdaRHITextureViewFormatCompatible(const FArdaRHITextureDesc& Texture, EArdaRHIFormat ViewFormat) noexcept
	{
		if (!IsArdaRHIFormatKnown(Texture.mFormat))
		{
			return false;
		}
		if (ViewFormat == EArdaRHIFormat::Unknown || ViewFormat == Texture.mFormat)
		{
			return true;
		}
		if (!IsArdaRHIFormatKnown(ViewFormat) || !HasAnyFlags(Texture.mUsage, EArdaRHITextureUsage::Typeless))
		{
			return false;
		}
		const auto BaseFormat = [](EArdaRHIFormat Format)
		{
			switch (Format)
			{
			case EArdaRHIFormat::SRGBA8UNorm:
				return EArdaRHIFormat::RGBA8UNorm;
			case EArdaRHIFormat::SBGRA8UNorm:
				return EArdaRHIFormat::BGRA8UNorm;
			case EArdaRHIFormat::D16:
				return EArdaRHIFormat::R16UNorm;
			case EArdaRHIFormat::D32:
				return EArdaRHIFormat::R32Float;
			default:
				return Format;
			}
		};
		const auto ResourceFormat = BaseFormat(Texture.mFormat);
		ViewFormat = BaseFormat(ViewFormat);
		if (ResourceFormat == ViewFormat)
		{
			return true;
		}
		// Enum spans contain exactly the typed representations of each storage family.
		constexpr EArdaRHIFormat Families[][2] = {{EArdaRHIFormat::R8UInt, EArdaRHIFormat::R8SNorm},
		    {EArdaRHIFormat::RG8UInt, EArdaRHIFormat::RG8SNorm},
		    {EArdaRHIFormat::R16UInt, EArdaRHIFormat::R16Float},
		    {EArdaRHIFormat::RGBA8UInt, EArdaRHIFormat::RGBA8SNorm},
		    {EArdaRHIFormat::RG16UInt, EArdaRHIFormat::RG16Float},
		    {EArdaRHIFormat::R32UInt, EArdaRHIFormat::R32Float},
		    {EArdaRHIFormat::RGBA16UInt, EArdaRHIFormat::RGBA16SNorm},
		    {EArdaRHIFormat::RG32UInt, EArdaRHIFormat::RG32Float},
		    {EArdaRHIFormat::RGB32UInt, EArdaRHIFormat::RGB32Float},
		    {EArdaRHIFormat::RGBA32UInt, EArdaRHIFormat::RGBA32Float},
		    {EArdaRHIFormat::BC1UNorm, EArdaRHIFormat::BC1UNormSRGB},
		    {EArdaRHIFormat::BC2UNorm, EArdaRHIFormat::BC2UNormSRGB},
		    {EArdaRHIFormat::BC3UNorm, EArdaRHIFormat::BC3UNormSRGB},
		    {EArdaRHIFormat::BC4UNorm, EArdaRHIFormat::BC4SNorm},
		    {EArdaRHIFormat::BC5UNorm, EArdaRHIFormat::BC5SNorm},
		    {EArdaRHIFormat::BC6HUFloat, EArdaRHIFormat::BC6HSFloat},
		    {EArdaRHIFormat::BC7UNorm, EArdaRHIFormat::BC7UNormSRGB}};
		for (const auto& Family : Families)
		{
			if (ResourceFormat >= Family[0] && ResourceFormat <= Family[1] && ViewFormat >= Family[0] &&
			    ViewFormat <= Family[1])
			{
				return true;
			}
		}
		return false;
	}

	uint32_t GetArdaRHITextureMipExtent(uint32_t BaseExtent, uint32_t MipLevel) noexcept
	{
		return MipLevel >= 32 ? 1u : eastl::max(1u, BaseExtent >> MipLevel);
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
		ArdaHashCombine(H, V.mClearValue.mR);
		ArdaHashCombine(H, V.mClearValue.mG);
		ArdaHashCombine(H, V.mClearValue.mB);
		ArdaHashCombine(H, V.mClearValue.mA);
		ArdaHashCombine(H, V.mbUseClearValue);
		ArdaHashString(H, V.mDebugName);
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHITextureDesc& D) noexcept
	{
		if (!D.mWidth || !D.mHeight || !D.mDepth || !D.mArraySize || !D.mMipLevels || !D.mSampleCount)
		{
			return Invalid("Texture dimensions, array size, mip count, and sample count must be non-zero.");
		}
		if (!IsArdaRHIFormatKnown(D.mFormat))
		{
			return Invalid("Texture format must be specified.");
		}
		if (D.mDimension <= EArdaRHITextureDimension::Unknown || D.mDimension > EArdaRHITextureDimension::Texture3D)
		{
			return Invalid("Texture dimension is invalid.");
		}
		constexpr auto KnownUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess |
		    EArdaRHITextureUsage::RenderTarget | EArdaRHITextureUsage::DepthStencil | EArdaRHITextureUsage::Typeless;
		if ((static_cast<uint16_t>(D.mUsage) & ~static_cast<uint16_t>(KnownUsage)) != 0)
		{
			return Invalid("Texture usage contains unknown flags.");
		}
		const bool b1D = D.mDimension == EArdaRHITextureDimension::Texture1D ||
		    D.mDimension == EArdaRHITextureDimension::Texture1DArray;
		const bool b3D = D.mDimension == EArdaRHITextureDimension::Texture3D;
		const bool bCube = D.mDimension == EArdaRHITextureDimension::TextureCube ||
		    D.mDimension == EArdaRHITextureDimension::TextureCubeArray;
		const bool bMultisampleDimension = D.mDimension == EArdaRHITextureDimension::Texture2DMS ||
		    D.mDimension == EArdaRHITextureDimension::Texture2DMSArray;
		const bool bArray = D.mDimension == EArdaRHITextureDimension::Texture1DArray ||
		    D.mDimension == EArdaRHITextureDimension::Texture2DArray ||
		    D.mDimension == EArdaRHITextureDimension::Texture2DMSArray || bCube;
		if ((b1D && D.mHeight != 1) || (!b3D && D.mDepth != 1) || (!bArray && D.mArraySize != 1))
		{
			return Invalid("Texture extents and array size do not match its dimension.");
		}
		if (bCube &&
		    (D.mWidth != D.mHeight || D.mArraySize % 6 != 0 ||
		        (D.mDimension == EArdaRHITextureDimension::TextureCube && D.mArraySize != 6)))
		{
			return Invalid("Cube textures require square faces and six layers per cube.");
		}
		if ((D.mSampleCount & (D.mSampleCount - 1)) != 0 || D.mSampleCount > 64 ||
		    (bMultisampleDimension && D.mSampleCount == 1))
		{
			return Invalid("Texture sample count must be a supported power of two and match its dimension.");
		}
		if (D.mSampleCount > 1 && (D.mMipLevels != 1 || b1D || b3D || bCube))
		{
			return Invalid("Multisampled textures require a two-dimensional shape and exactly one mip level.");
		}
		uint32_t MaxMipLevels = 1;
		for (uint32_t Extent = eastl::max(D.mWidth, eastl::max(D.mHeight, D.mDepth)); Extent > 1; Extent >>= 1)
		{
			++MaxMipLevels;
		}
		if (D.mMipLevels > MaxMipLevels)
		{
			return Invalid("Texture mip count exceeds the chain permitted by its extents.");
		}
		const auto& Format = GetArdaRHIFormatInfo(D.mFormat);
		const bool bDepthAttachment = HasAnyFlags(D.mUsage, EArdaRHITextureUsage::DepthStencil);
		const bool bColorAttachment = HasAnyFlags(D.mUsage, EArdaRHITextureUsage::RenderTarget);
		const bool bStorage = HasAnyFlags(D.mUsage, EArdaRHITextureUsage::UnorderedAccess);
		if ((bDepthAttachment && !Format.mbDepth) || (Format.mbDepth && (bColorAttachment || bStorage)) ||
		    (Format.mBlockWidth > 1 && (bDepthAttachment || bColorAttachment || bStorage || D.mSampleCount > 1)))
		{
			return Invalid("Texture format is incompatible with its attachment, storage, or multisample usage.");
		}
		constexpr auto TextureStates = EArdaRHIResourceState::Common | EArdaRHIResourceState::ShaderResource |
		    EArdaRHIResourceState::UnorderedAccess | EArdaRHIResourceState::RenderTarget |
		    EArdaRHIResourceState::DepthRead | EArdaRHIResourceState::DepthWrite | EArdaRHIResourceState::CopySource |
		    EArdaRHIResourceState::CopyDest | EArdaRHIResourceState::ResolveSource |
		    EArdaRHIResourceState::ResolveDest | EArdaRHIResourceState::Present | EArdaRHIResourceState::Discard |
		    EArdaRHIResourceState::ShadingRateSource;
		if ((static_cast<uint32_t>(D.mInitialState) & ~static_cast<uint32_t>(TextureStates)) != 0)
		{
			return Invalid("Texture initial state contains an unknown or buffer-only state.");
		}
		if (!AllowsInitialState(D, EArdaRHIResourceState::ShaderResource, EArdaRHITextureUsage::ShaderResource) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::UnorderedAccess, EArdaRHITextureUsage::UnorderedAccess) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::RenderTarget, EArdaRHITextureUsage::RenderTarget) ||
		    !AllowsInitialState(D,
		        EArdaRHIResourceState::DepthRead | EArdaRHIResourceState::DepthWrite,
		        EArdaRHITextureUsage::DepthStencil))
		{
			return Invalid("Texture initial state requires its matching declared usage.");
		}
		if (D.mbVirtual && D.mbTiled)
		{
			return Invalid("Texture storage cannot be both virtual and tiled.");
		}
		return {};
	}

	FArdaRHIStatus ValidateResourceCapabilities(const FArdaRHITextureDesc& D,
	    const FArdaRHICapabilities& C,
	    const FArdaRHIFormatSupport& F) noexcept
	{
		if (const auto Status = Validate(D); !Status)
		{
			return Status;
		}
		const auto Unsupported = [](const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Message);
		};
		const bool b1D = D.mDimension == EArdaRHITextureDimension::Texture1D ||
		    D.mDimension == EArdaRHITextureDimension::Texture1DArray;
		const bool b3D = D.mDimension == EArdaRHITextureDimension::Texture3D;
		const bool bCube = D.mDimension == EArdaRHITextureDimension::TextureCube ||
		    D.mDimension == EArdaRHITextureDimension::TextureCubeArray;
		const uint32_t MaxExtent = b1D ? C.mLimits.mMaxTexture1D
		    : b3D                      ? C.mLimits.mMaxTexture3D
		    : bCube                    ? C.mLimits.mMaxTextureCube
		                               : C.mLimits.mMaxTexture2D;
		if ((MaxExtent && eastl::max(D.mWidth, eastl::max(D.mHeight, D.mDepth)) > MaxExtent) ||
		    (C.mLimits.mMaxTextureArrayLayers && D.mArraySize > C.mLimits.mMaxTextureArrayLayers))
		{
			return Unsupported("Texture extents or array layers exceed the device limits.");
		}
		if ((D.mbVirtual && !C.mbVirtualResources) ||
		    (D.mbTiled && (b3D ? !C.mResidency.mbReservedTexture3D : b1D || !C.mResidency.mbReservedTexture2D)))
		{
			return Unsupported("Requested virtual or tiled texture storage is unsupported by the device.");
		}
		if (!C.mbFormatSupportReported)
		{
			return {};
		}
		const bool bDimension = b1D ? F.mbTexture1D : b3D ? F.mbTexture3D : bCube ? F.mbTextureCube : F.mbTexture2D;
		if (!F.mNativeFormat || !bDimension || !(F.mSampleCounts & D.mSampleCount))
		{
			return Unsupported("Texture format, dimension, or sample count is unsupported by the device.");
		}
		if ((HasAnyFlags(D.mUsage, EArdaRHITextureUsage::ShaderResource) && !F.mbShaderResource) ||
		    (HasAnyFlags(D.mUsage, EArdaRHITextureUsage::UnorderedAccess) && !F.mbStorage) ||
		    (HasAnyFlags(D.mUsage, EArdaRHITextureUsage::RenderTarget) && !F.mbColorAttachment) ||
		    (HasAnyFlags(D.mUsage, EArdaRHITextureUsage::DepthStencil) && !F.mbDepthStencilAttachment))
		{
			return Unsupported("Texture format does not support a requested usage on this device.");
		}
		return {};
	}
}
