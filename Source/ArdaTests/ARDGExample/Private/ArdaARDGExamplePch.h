#pragma once

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <Windows.h>
#endif

#if defined(ARDA_TEST_WITH_VULKAN)
    #include <vulkan/vulkan.h>
#endif
#include <GLFW/glfw3.h>

#include "RHIWrappers/ArdaRHI.h"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/optional.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
