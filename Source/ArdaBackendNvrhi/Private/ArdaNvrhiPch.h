/** @file ArdaNvrhiPch.h
 *  Collects platform, graphics API, NVRHI, and utility headers used by the backend.
 */
#pragma once

#if defined(_WIN32)
    #ifndef NOMINMAX
        /** Prevents Windows headers from defining the min and max macros. */
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        /** Excludes rarely used declarations from Windows headers. */
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <Windows.h>
    #if defined(ARDA_NVRHI_WITH_D3D12)
        #include <d3d12.h>
        #include <dxgi1_6.h>
        #include <wrl/client.h>
    #endif
#endif

#include <nvrhi/nvrhi.h>
#include <nvrhi/validation.h>
#if defined(ARDA_NVRHI_WITH_VULKAN)
    /** Selects Vulkan-Hpp's runtime-configurable dispatch loader. */
    #define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
    /** Configures Vulkan-Hpp to report failures without C++ exceptions. */
    #define VULKAN_HPP_NO_EXCEPTIONS 1
    #include <vulkan/vulkan.hpp>
    #include <nvrhi/vulkan.h>
#endif
#if defined(_WIN32) && defined(ARDA_NVRHI_WITH_D3D12)
    #include <nvrhi/d3d12.h>
#endif

#include <cstdio>
#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <mutex>
#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>
