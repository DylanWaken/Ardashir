#pragma once

#include "CUDA/ArdaNativeCudaProvider.h"

namespace arda
{
	using FArdaCudaModeCheck = FArdaRHIStatus (*)(const char* Backend, EArdaCudaLaunchMode Mode);

	eastl::shared_ptr<const IArdaNativeComputeProvider> MakeArdaNativeCudaProvider(const char* Id,
	    eastl::vector<eastl::string> GraphicsApis, FArdaCudaModeCheck CheckMode);
}
