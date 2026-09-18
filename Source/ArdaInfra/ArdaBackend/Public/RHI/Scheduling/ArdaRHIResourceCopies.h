/** @file ArdaRHIResourceCopies.h
 * Declares ResourceCopies definitions for the RHI scheduling module.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHITexture.h"

#include <cstdint>

namespace arda
{
	/** Describes texture slice. */
	struct FArdaRHITextureSlice
	{
		/** X origin in texels. */
		uint32_t mX = 0;
		/** Y origin in texels. */
		uint32_t mY = 0;
		/** Z origin in texels. */
		uint32_t mZ = 0;
		/** Stores the width. */
		uint32_t mWidth = ArdaRHIAllSubresources;
		/** Stores the height. */
		uint32_t mHeight = ArdaRHIAllSubresources;
		/** Stores the depth. */
		uint32_t mDepth = ArdaRHIAllSubresources;
		/** Mip level containing the slice. */
		uint32_t mMipLevel = 0;
		/** Array slice containing the region. */
		uint32_t mArraySlice = 0;
		/** Format plane containing the region. */
		uint32_t mPlane = 0;
	};

	/** Pitched layout of one texture region in a buffer, without format conversion.
     * Rows are contiguous within each depth slice; the slice pitch is row pitch times region height.
     */
	struct FArdaRHITextureBufferLayout
	{
		/** Offset of the first texel, aligned to 512 bytes and divisible by the texel size. */
		uint64_t mByteOffset = 0;
		/** Bytes between rows: nonzero, at most INT32_MAX, aligned to 256 bytes and whole texels. */
		uint32_t mRowPitch = 0;
	};

	/** Concrete extent resolved for a texture-region copy. */
	struct FArdaRHITextureCopyExtent
	{
		uint32_t mWidth = 0;
		uint32_t mHeight = 0;
		uint32_t mDepth = 0;
	};

	/** Portable pitched buffer footprint for one texture region. */
	struct FArdaRHITextureBufferFootprint
	{
		/** Concrete copied extent in texels. */
		FArdaRHITextureCopyExtent mExtent;
		/** Zero-based buffer layout, aligned for both D3D12 and Vulkan copies. */
		FArdaRHITextureBufferLayout mLayout;
		/** Number of meaningful bytes in each row, excluding padding. */
		uint64_t mRowBytes = 0;
		/** Total rows across all depth slices in the region. */
		uint64_t mRowCount = 0;
		/** Minimum allocation size, excluding padding after the final row. */
		uint64_t mByteSize = 0;
	};

	/**
     * Validates matching texture slices and resolves sentinel source extents.
     * @return Success and a concrete non-empty copy extent, or a validation error.
     */
	[[nodiscard]] FArdaRHIStatus ResolveArdaRHITextureCopyExtent(const FArdaRHITextureDesc& DestinationDesc,
	    const FArdaRHITextureSlice& DestinationSlice,
	    const FArdaRHITextureDesc& SourceDesc,
	    const FArdaRHITextureSlice& SourceSlice,
	    FArdaRHITextureCopyExtent& OutExtent) noexcept;

	/** Validates a single-sample, uncompressed color region and its pitched buffer range.
     * Explicit extents must fit the mip; sentinel extents select the remaining mip region.
     * The buffer must contain all addressed texels; padding after the final row is optional.
     */
	[[nodiscard]] FArdaRHIStatus ValidateArdaRHITextureBufferCopy(const FArdaRHITextureDesc& TextureDesc,
	    const FArdaRHITextureSlice& Slice,
	    const FArdaRHIBufferDesc& BufferDesc,
	    const FArdaRHITextureBufferLayout& Layout,
	    FArdaRHITextureCopyExtent& OutExtent) noexcept;

	/** Resolves a portable buffer footprint for a typed, single-sample, uncompressed color region.
	 * Row pitch aligns to 256 bytes and whole texels, including formats with twelve-byte texels.
	 * Explicit extents must fit the mip; sentinel extents select its remaining region.
	 * @return A validated footprint, or an error for unsupported regions or overflowing sizes.
	 */
	[[nodiscard]] TArdaRHIResult<FArdaRHITextureBufferFootprint> GetArdaRHITextureBufferFootprint(
	    const FArdaRHITextureDesc& TextureDesc,
	    const FArdaRHITextureSlice& Slice) noexcept;

	/**
     * Validates a whole-subresource multisample resolve and returns its extent.
     * Slice width, height, and depth are ignored because resolves are not regions.
     */
	[[nodiscard]] FArdaRHIStatus ValidateArdaRHITextureResolve(const FArdaRHITextureDesc& DestinationDesc,
	    const FArdaRHITextureSlice& DestinationSlice,
	    const FArdaRHITextureDesc& SourceDesc,
	    const FArdaRHITextureSlice& SourceSlice,
	    FArdaRHITextureCopyExtent& OutExtent) noexcept;
}
