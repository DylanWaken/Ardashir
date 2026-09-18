#pragma once

#include "RHI/Shaders/ArdaShaderType.h"

#include <memory>
#include <mutex>

namespace arda
{
	// Process-wide shader registrations and committed snapshots.
	struct FArdaShaderRegistryContext
	{
		std::mutex mMutex;
		eastl::vector<std::shared_ptr<FArdaShaderType>> mNodes;
		eastl::vector<std::shared_ptr<FArdaShaderType>> mCommitted;
		eastl::vector<std::shared_ptr<FArdaShaderType>> mRetired;
		uint64_t mGeneration = 0;
	};
}
