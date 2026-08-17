/** @file ArdaNvrhiExternalDeviceTypes.h
 *  @brief Declares private NVRHI external-device translation structures.
 */
#pragma once

#include "ArdaExternalInterop.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace arda::backend
{
    /** Describes an externally owned Direct3D 12 device for the NVRHI adapter. */
    struct FArdaNvrhiExternalD3D12DeviceDesc
    {
        FArdaNativeObject mDevice;
        FArdaNativeObject mGraphicsQueue;
        FArdaNativeObject mComputeQueue;
        FArdaNativeObject mCopyQueue;
        FArdaNativeObject mDxgiFactory;
        uint32_t mRenderTargetViewHeapSize = 1024;
        uint32_t mDepthStencilViewHeapSize = 1024;
        uint32_t mShaderResourceViewHeapSize = 16384;
        uint32_t mSamplerHeapSize = 1024;
        uint32_t mMaxTimerQueries = 256;
        bool mbEnableHeapDirectlyIndexed = false;
        bool mbAftermathEnabled = false;
        bool mbLogBufferLifetime = false;
        bool mbEnableRayTracingValidation = false;
        bool mbEnableEnhancedBarriers = true;
    };

    /** Identifies an externally owned Vulkan queue for the NVRHI adapter. */
    struct FArdaNvrhiExternalVulkanQueueDesc
    {
        FArdaNativeObject mQueue;
        uint32_t mFamilyIndex = 0;
        uint32_t mQueueIndex = 0;
    };

    /** Describes an externally owned Vulkan device for the NVRHI adapter. */
    struct FArdaNvrhiExternalVulkanDeviceDesc
    {
        FArdaNativeObject mInstance;
        FArdaNativeObject mPhysicalDevice;
        FArdaNativeObject mDevice;
        FArdaNvrhiExternalVulkanQueueDesc mGraphicsQueue;
        FArdaNvrhiExternalVulkanQueueDesc mComputeQueue;
        FArdaNvrhiExternalVulkanQueueDesc mCopyQueue;
        eastl::vector<eastl::string> mInstanceExtensions;
        eastl::vector<eastl::string> mDeviceExtensions;
        FArdaNativeObject mAllocationCallbacks;
        bool mbBufferDeviceAddressSupported = false;
        bool mbAftermathEnabled = false;
        bool mbLogBufferLifetime = false;
        uint32_t mMaxTimerQueries = 256;
        eastl::string mVulkanLibraryName;
    };

}
