/** Private process-wide registry context. */
#pragma once

#include "RHI/Interop/ArdaExternalInterop.h"
#include <mutex>

namespace arda
{
	struct FArdaResourceProviderEntry
	{
		eastl::string mName;
		IArdaExternalResourceProvider* mProvider = nullptr;
	};

	struct FArdaResourceProviderRegistry
	{
		std::mutex mMutex;
		eastl::vector<FArdaResourceProviderEntry> mEntries;
	};


	FArdaResourceProviderRegistry& GetResourceProviderRegistry();
}
