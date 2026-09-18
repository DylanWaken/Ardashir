#pragma once

#include "RHI/Shaders/ArdaShaderType.h"

#include <EASTL/shared_ptr.h>
#include <mutex>

namespace arda
{
	// Process-wide shader registrations and committed snapshots.
	struct FArdaShaderRegistryContext
	{
		std::mutex mMutex;
		eastl::vector<eastl::shared_ptr<FArdaShaderType>> mNodes;
		eastl::vector<eastl::shared_ptr<FArdaShaderType>> mCommitted;
		eastl::vector<eastl::shared_ptr<FArdaShaderType>> mRetired;
		uint64_t mGeneration = 0;
	};
}
