/** Private process-wide registry context. */
#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <mutex>

namespace arda
{
	class IArdaExternalResourceProvider;

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
