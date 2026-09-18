/** @file ArdaRHITiling.h
 * Declares Tiling definitions for the RHI memory module.
 */

#pragma once

#include "RHI/Memory/ArdaRHIHeap.h"
#include "RHI/Resources/ArdaRHIRef.h"

#include <EASTL/vector.h>
#include <cstdint>

namespace arda
{
	/** Describes tiled texture coordinate. */
	struct FArdaRHITiledTextureCoordinate
	{
		/** Mip level containing the tile. */
		uint16_t mMipLevel = 0;
		/** Array level containing the tile. */
		uint16_t mArrayLevel = 0;
		/** Tile X coordinate. */
		uint32_t mX = 0;
		/** Tile Y coordinate. */
		uint32_t mY = 0;
		/** Tile Z coordinate. */
		uint32_t mZ = 0;
	};

	/** Describes tiled texture region. */
	struct FArdaRHITiledTextureRegion
	{
		/** Number of tiles in the region. */
		uint32_t mTileCount = 0;
		/** Region width in tiles. */
		uint32_t mWidth = 0;
		/** Region height in tiles. */
		uint32_t mHeight = 0;
		/** Region depth in tiles. */
		uint32_t mDepth = 0;
	};

	/** Describes packed mip desc. */
	struct FArdaRHIPackedMipDesc
	{
		/** Number of standard tiled mip levels. */
		uint32_t mStandardMipCount = 0;
		/** Number of packed mip levels. */
		uint32_t mPackedMipCount = 0;
		/** Number of tiles occupied by packed mips. */
		uint32_t mPackedMipTileCount = 0;
		/** First tile index used by packed mips. */
		uint32_t mStartTileIndex = 0;
	};

	/** Describes tile shape. */
	struct FArdaRHITileShape
	{
		/** Tile width in texels. */
		uint32_t mWidthInTexels = 0;
		/** Tile height in texels. */
		uint32_t mHeightInTexels = 0;
		/** Tile depth in texels. */
		uint32_t mDepthInTexels = 0;
	};

	/** Describes subresource tiling. */
	struct FArdaRHISubresourceTiling
	{
		/** Subresource width in tiles. */
		uint32_t mWidthInTiles = 0;
		/** Subresource height in tiles. */
		uint32_t mHeightInTiles = 0;
		/** Subresource depth in tiles. */
		uint32_t mDepthInTiles = 0;
		/** Stores the start tile index. */
		uint32_t mStartTileIndex = 0;
	};

	/** Describes texture tile mapping. */
	struct FArdaRHITextureTileMapping
	{
		/** Stores the coordinates. */
		eastl::vector<FArdaRHITiledTextureCoordinate> mCoordinates;
		/** Stores the regions. */
		eastl::vector<FArdaRHITiledTextureRegion> mRegions;
		/** Stores the byte offsets. */
		eastl::vector<uint64_t> mByteOffsets;
		/** Stores the heap. */
		FArdaRHIHeapRef mHeap;
	};

	/** Describes texture tiling. */
	struct FArdaRHITextureTiling
	{
		/** Stores the tile count. */
		uint32_t mTileCount = 0;
		/** Stores the packed mips. */
		FArdaRHIPackedMipDesc mPackedMips;
		/** Stores the tile shape. */
		FArdaRHITileShape mTileShape;
		/** Stores the subresources. */
		eastl::vector<FArdaRHISubresourceTiling> mSubresources;
	};

	/** Generic contiguous tile mapping for sparse/reserved buffers. */
	struct FArdaRHIBufferTileMapping
	{
		uint64_t mBufferOffset = 0;
		uint64_t mByteSize = 0;
		uint64_t mHeapOffset = 0;
		FArdaRHIHeapRef mHeap;
		bool mbCommit = true;
	};

	/** Current local or non-local GPU memory budget telemetry. */
	struct FArdaRHIStreamingBudget
	{
		uint64_t mBudgetBytes = 0;
		uint64_t mCurrentUsageBytes = 0;
		uint64_t mAvailableForReservationBytes = 0;
		uint64_t mCurrentReservationBytes = 0;
		bool mbLocalMemory = true;
	};
}
