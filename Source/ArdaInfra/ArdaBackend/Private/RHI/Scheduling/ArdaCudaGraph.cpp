#include <mutex>
#include "RHI/Scheduling/ArdaCudaGraph.h"

namespace arda
{
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
}
