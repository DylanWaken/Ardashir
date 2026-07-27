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

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <nvrhi/nvrhi.h>
#include <nvrhi/utils.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <EASTL/numeric_limits.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/optional.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
