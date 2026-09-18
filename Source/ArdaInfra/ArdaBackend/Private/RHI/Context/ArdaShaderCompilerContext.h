#pragma once

#include "RHI/Shaders/ArdaShaderCompiler.h"

#include <EASTL/map.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>

#include <mutex>

namespace arda
{
	// Process-wide compiler configuration and serialization for shared output artifacts.
	struct FArdaShaderCompilerContext
	{
		std::mutex mConfigurationMutex;
		FArdaShaderCompilerConfiguration mConfiguration;
		std::mutex mOutputMutexMapMutex;
		eastl::map<eastl::string, eastl::weak_ptr<std::mutex>> mOutputMutexes;
	};
}
