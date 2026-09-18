#pragma once

#include "RHI/Shaders/ArdaShaderCompiler.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace arda
{
	// Process-wide compiler configuration and serialization for shared output artifacts.
	struct FArdaShaderCompilerContext
	{
		std::mutex mConfigurationMutex;
		FArdaShaderCompilerConfiguration mConfiguration;
		std::mutex mOutputMutexMapMutex;
		std::map<std::string, std::weak_ptr<std::mutex>> mOutputMutexes;
	};
}
