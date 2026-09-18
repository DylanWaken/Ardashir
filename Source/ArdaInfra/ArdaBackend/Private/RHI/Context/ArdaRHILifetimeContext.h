/** Shared ownership identity and live-resource counters for one device generation. */
#pragma once

#include "RHI/Resources/ArdaRHIResource.h"
#include <EASTL/atomic.h>

namespace arda::detail
{
	class FArdaLifetimeTracker
	{
	public:
		void Add(EArdaRHIResourceType Type) noexcept
		{
			mLive[static_cast<size_t>(Type)].fetch_add(1, eastl::memory_order_relaxed);
		}

		void Remove(EArdaRHIResourceType Type) noexcept
		{
			mLive[static_cast<size_t>(Type)].fetch_sub(1, eastl::memory_order_relaxed);
		}

		size_t Get(EArdaRHIResourceType Type) const noexcept
		{
			return mLive[static_cast<size_t>(Type)].load(eastl::memory_order_relaxed);
		}

	private:
		eastl::atomic<size_t> mLive[static_cast<size_t>(EArdaRHIResourceType::Count)]{};
	};
}
