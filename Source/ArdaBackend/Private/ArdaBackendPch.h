#pragma once

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <Windows.h>
    #include <d3d12.h>
    #include <dxgi1_6.h>
    #include <wrl/client.h>
#endif

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_EXCEPTIONS 1
#include <vulkan/vulkan.hpp>

#include <nvrhi/nvrhi.h>
#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>
#if defined(_WIN32)
    #include <nvrhi/d3d12.h>
#endif

#include <cstdio>
#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <mutex>
#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>
