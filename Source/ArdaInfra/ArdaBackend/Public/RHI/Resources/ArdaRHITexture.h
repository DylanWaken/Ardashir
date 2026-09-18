/** @file ArdaRHITexture.h
 * Declares Texture definitions for the RHI resources module.
 */

#pragma once
#include <EASTL/numeric_limits.h>

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Memory/ArdaRHIMemoryTypes.h"
#include "RHI/Resources/ArdaRHIColor.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Scheduling/ArdaRHIResourceStates.h"

#include <EASTL/string.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** CPU mapping of one staging-texture region, valid until the texture is unmapped. */
	struct FArdaRHIStagingTextureMapping
	{
		/** First mapped byte. */
		void* mData = nullptr;
		/** Byte distance between adjacent rows. */
		size_t mRowPitch = 0;
		/** Byte distance between adjacent depth slices of the mapped mip. */
		size_t mDepthPitch = 0;
	};

	/** Forward declaration of texture desc. */
	struct FArdaRHITextureDesc;

	/**
     * Performs the max operation.
     * @return The requested numeric value.
     */
	inline constexpr uint32_t ArdaRHIAllSubresources = eastl::numeric_limits<uint32_t>::max();

	/** Enumerates texture dimension values. */
	enum class EArdaRHITextureDimension : uint8_t
	{
		Unknown,
		Texture1D,
		Texture1DArray,
		Texture2D,
		Texture2DArray,
		TextureCube,
		TextureCubeArray,
		Texture2DMS,
		Texture2DMSArray,
		Texture3D
	};

	/** Enumerates texture usage values. */
	enum class EArdaRHITextureUsage : uint16_t
	{
		None = 0,
		ShaderResource = 1u << 0,
		UnorderedAccess = 1u << 1,
		RenderTarget = 1u << 2,
		DepthStencil = 1u << 3,
		Typeless = 1u << 4
	};

	constexpr EArdaRHITextureUsage operator|(EArdaRHITextureUsage A, EArdaRHITextureUsage B) noexcept
	{
		return static_cast<EArdaRHITextureUsage>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
	}
	constexpr EArdaRHITextureUsage operator&(EArdaRHITextureUsage A, EArdaRHITextureUsage B) noexcept
	{
		return static_cast<EArdaRHITextureUsage>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
	}
	constexpr EArdaRHITextureUsage& operator|=(EArdaRHITextureUsage& A, EArdaRHITextureUsage B) noexcept
	{
		A = A | B;
		return A;
	}
	constexpr bool HasAnyFlags(EArdaRHITextureUsage Value, EArdaRHITextureUsage Flags) noexcept
	{
		return static_cast<uint32_t>(Value & Flags) != 0;
	}

	/** Describes texture subresource range. */
	struct FArdaRHITextureSubresourceRange
	{
		/** Stores the base mip level. */
		uint32_t mBaseMipLevel = 0;
		/** Stores the mip level count. */
		uint32_t mMipLevelCount = ArdaRHIAllSubresources;
		/** Stores the base array slice. */
		uint32_t mBaseArraySlice = 0;
		/** Stores the array slice count. */
		uint32_t mArraySliceCount = ArdaRHIAllSubresources;
		/** First format plane (depth is zero and stencil is one). */
		uint32_t mBasePlane = 0;
		/** Number of format planes. */
		uint32_t mPlaneCount = ArdaRHIAllSubresources;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHITextureSubresourceRange& O) const noexcept
		{
			return mBaseMipLevel == O.mBaseMipLevel && mMipLevelCount == O.mMipLevelCount &&
			    mBaseArraySlice == O.mBaseArraySlice && mArraySliceCount == O.mArraySliceCount &&
			    mBasePlane == O.mBasePlane && mPlaneCount == O.mPlaneCount;
		}

		/**
         * Performs the resolve operation.
         * @param Desc The desc.
         * @return The requested value.
         */
		[[nodiscard]] FArdaRHITextureSubresourceRange Resolve(const FArdaRHITextureDesc& Desc) const noexcept;
	};

	/** Describes texture desc. */
	struct FArdaRHITextureDesc
	{
		/** Texture width in texels. */
		uint32_t mWidth = 1;
		/** Texture height in texels; one for 1D textures. */
		uint32_t mHeight = 1;
		/** Texture depth in texels; one except for 3D textures. */
		uint32_t mDepth = 1;
		/** Array layers; six per cube, exactly six for TextureCube, and one for non-array/3D textures. */
		uint32_t mArraySize = 1;
		/** Mip levels, at most floor(log2(maximum extent)) + 1; multisampled textures require one. */
		uint32_t mMipLevels = 1;
		/** Power-of-two sample count; 2D/2DArray may also request MSAA without explicit MS dimensions. */
		uint32_t mSampleCount = 1;
		/** Stores the format. */
		EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
		/** Stores the dimension. */
		EArdaRHITextureDimension mDimension = EArdaRHITextureDimension::Texture2D;
		/** Stores the usage. */
		EArdaRHITextureUsage mUsage = EArdaRHITextureUsage::ShaderResource;
		/** Stores the initial state. */
		EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
		/** Stores the keep initial state. */
		bool mbKeepInitialState = false;
		/** Stores the virtual. */
		bool mbVirtual = false;
		/** Stores the tiled. */
		bool mbTiled = false;
		/** Stores the clear value. */
		FArdaRHIColor mClearValue;
		/** Stores the use clear value. */
		bool mbUseClearValue = false;
		/** Stores the debug name. */
		eastl::string mDebugName;
		/** Request CUDA-compatible native allocation; unsupported descriptors fail creation. */
		bool mbCudaInterop = false;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHITextureDesc& O) const noexcept;
	};

	/** Unknown inherits the texture format; other reinterpretations require Typeless and the same format family. */
	[[nodiscard]] bool IsArdaRHITextureViewFormatCompatible(const FArdaRHITextureDesc& Texture,
	    EArdaRHIFormat ViewFormat) noexcept;

	/** @return One dimension of a texture at the requested mip level. */
	[[nodiscard]] uint32_t GetArdaRHITextureMipExtent(uint32_t BaseExtent, uint32_t MipLevel) noexcept;

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHITextureSubresourceRange& Value) noexcept;

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHITextureDesc& Value) noexcept;

	/**
     * Central descriptor validation used before cache lookup and native creation.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHITextureDesc& Value) noexcept;

	/** Interface for texture. */
	class IArdaRHITexture : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHITextureDesc& GetDesc() const noexcept = 0;

		/** Returns the physical identity. */
		[[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
	};

	/** Mutable logical indirection to a texture; the referenced texture is retained. */
	class IArdaRHITextureReference : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the texture.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHITextureRef& GetTexture() const noexcept = 0;
	};

	/** Describes staging texture desc. */
	struct FArdaRHIStagingTextureDesc
	{
		/** Stores the texture. */
		FArdaRHITextureDesc mTexture;
		/** Stores the CPU access. */
		EArdaRHICpuAccess mCpuAccess = EArdaRHICpuAccess::Read;
		/** Stores the debug name. */
		eastl::string mDebugName;
	};

	/** Interface for staging texture. */
	class IArdaRHIStagingTexture : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIStagingTextureDesc& GetDesc() const noexcept = 0;
	};
}
