/** Private process-wide registry context. */
#pragma once
#include <EASTL/atomic.h>

#include "RHI/Providers/ArdaBackendProvider.h"
#include <mutex>

namespace arda
{
	struct FArdaBackendModuleEntry
	{
		IArdaBackendModule* mModule = nullptr;
	};

	struct FArdaBackendModuleRegistry
	{
		std::mutex mMutex;
		eastl::vector<FArdaBackendModuleEntry> mEntries;
		eastl::atomic<const IArdaBackendModule*> mActiveModule{nullptr};
	};


	FArdaBackendModuleRegistry& GetBackendModuleRegistry();
}
