/** @file ArdaCudaGraph.h
 * Retained CUDA graph caches and sequence recording metadata.
 */
#pragma once

#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Scheduling/ArdaCudaTiming.h"

namespace arda
{
	/** Policy for retaining a native CUDA Graph executable across sequence recordings. */
	enum class EArdaCudaGraphMode : uint8_t
	{
		Disabled,
		Prefer,
		Require
	};

	/** Cumulative counters for one retained sequence cache; recording alone can capture a graph. */
	struct FArdaCudaGraphStats
	{
		uint64_t mCaptureCount = 0;
		uint64_t mReplayCount = 0;
		uint64_t mRebuildCount = 0;
		uint64_t mFallbackCount = 0;
		uint64_t mCaptureFailureCount = 0;
		uint64_t mCacheHitCount = 0;
		uint64_t mEvictionCount = 0;
		uint32_t mCachedVariantCount = 0;
		eastl::string mLastFallbackReason;
	};

	/** Retains a bounded LRU of native executables. Exact resolved arguments and resource identities
	 * determine reuse; new variants evict the least recently recorded executable at capacity.
	 * In-flight users retain evicted entries. Use separate caches per logical batch and frame slot.
	 */
	class FArdaCudaGraphCache
	{
	public:
		explicit FArdaCudaGraphCache(EArdaCudaGraphMode Mode = EArdaCudaGraphMode::Prefer,
		    uint32_t MaximumCachedVariants = 4);
		~FArdaCudaGraphCache();
		[[nodiscard]] EArdaCudaGraphMode GetMode() const noexcept;
		[[nodiscard]] uint32_t GetMaximumCachedVariants() const noexcept;
		[[nodiscard]] FArdaCudaGraphStats GetStats() const;
		/** Evicts all variants without resetting counters or invalidating in-flight work. */
		void Reset();

	private:
		friend struct FArdaCudaGraphCacheAccess;
		const EArdaCudaGraphMode mMode;
		const uint32_t mMaximumCachedVariants;
		mutable std::mutex mMutex;
		FArdaCudaGraphStats mStats;
		eastl::shared_ptr<void> mNativeState;
	};

	/** Provider metadata shared by every operation in one captured sequence recording.
	 * FArdaCudaSequence supplies this automatically; resources include unpatched scratch bindings.
	 */
	struct FArdaCudaGraphBatch
	{
		eastl::shared_ptr<FArdaCudaGraphCache> mCache;
		eastl::vector<FArdaRHIResourceRef> mResources;
		eastl::shared_ptr<FArdaCudaTimingQuery> mTimingQuery;
		eastl::vector<FArdaCudaTimingRegion> mTimingRegions;
	};
}
