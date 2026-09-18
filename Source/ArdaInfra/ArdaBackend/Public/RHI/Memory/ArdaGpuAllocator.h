/** @file ArdaGpuAllocator.h
 * Device-wide GPU storage cache policy and allocation diagnostics.
 */
#pragma once

#include <cstdint>

namespace arda
{
	/** Idle storage is retained across graphs; active leases are never evicted.
	 * Collection cycles advance through IArdaRHIDevice::RunGarbageCollection.
	 */
	struct FArdaGpuAllocatorOptions
	{
		/** Idle collection cycles before an entire heap may be released. */
		uint32_t mHeapRetentionCycles = 16;
		/** Minimum idle age for resource eviction when its cache exceeds capacity. */
		uint32_t mResourceRetentionCycles = 32;
		/** Soft buffer-object cache capacity per heap or committed-resource pool. */
		uint32_t mBufferCacheCapacity = 64;
		/** Soft texture-object cache capacity per heap or committed-resource pool. */
		uint32_t mTextureCacheCapacity = 64;
		/** Maximum idle backing storage retained after collection; zero disables retention. */
		uint64_t mMaxCachedBytes = 512ull * 1024 * 1024;
	};

	/** Native allocation counters and retained storage for one backend device.
	 * Placed resources are accounted through their parent heap, never twice.
	 */
	struct FArdaGpuAllocatorStats
	{
		/** Native heap creation count since device initialization. */
		uint64_t mHeapCreations = 0;
		/** Native buffer creation count, including virtual object creation. */
		uint64_t mBufferCreations = 0;
		/** Native texture creation count, including virtual object creation. */
		uint64_t mTextureCreations = 0;
		/** Heap acquisitions satisfied by cached storage. */
		uint64_t mHeapCacheHits = 0;
		/** Buffer acquisitions satisfied without native object creation. */
		uint64_t mBufferCacheHits = 0;
		/** Texture acquisitions satisfied without native object creation. */
		uint64_t mTextureCacheHits = 0;
		/** Bytes in active and cached heaps managed by this allocator. */
		uint64_t mHeapBytes = 0;
		/** Heap bytes currently available for another lease. */
		uint64_t mCachedHeapBytes = 0;
		/** Bytes in active and cached committed resources managed by this allocator. */
		uint64_t mCommittedBytes = 0;
		/** Committed bytes currently available for another lease. */
		uint64_t mCachedCommittedBytes = 0;
		/** Number of idle buffer and texture objects, including placed objects. */
		uint64_t mCachedResources = 0;
		/** Number of completed collection cycles. */
		uint64_t mCollectionCycle = 0;
	};
}
