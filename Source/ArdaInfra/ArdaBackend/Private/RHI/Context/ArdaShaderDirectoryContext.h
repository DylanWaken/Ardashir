#pragma once

#include "RHI/Shaders/ArdaShaderDirectories.h"

#include <mutex>

namespace arda
{
	// Process-wide registration and backend lifecycle state for shader source directories.
	struct FArdaShaderDirectoryContext
	{
		std::mutex mMutex;
		eastl::vector<FArdaShaderSourceDirectory> mDirectories;
		eastl::vector<FArdaShaderSourceFile> mFiles;
		FArdaShaderDirectoryStatus mLastStatus;
		bool mbFrozen = false;
		bool mbRegistryInUse = false;
		bool mbBackendInitialized = false;
	};
}
