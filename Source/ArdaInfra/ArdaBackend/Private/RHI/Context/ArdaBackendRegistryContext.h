/** Private process-wide registry context. */
#pragma once

#include "RHI/Providers/ArdaBackendProvider.h"
#include <atomic>
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
		std::atomic<const IArdaBackendModule*> mActiveModule{nullptr};
	};


	FArdaBackendModuleRegistry& GetBackendModuleRegistry();
}
