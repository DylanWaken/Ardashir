#pragma once

#include "RHIWrappers/ArdaRHI.h"

#include <EASTL/shared_ptr.h>
#include <nvrhi/nvrhi.h>

namespace arda::rhi::private_impl
{
    /** Creates the opaque Arda facade for an existing NVRHI device. */
    [[nodiscard]] FArdaRHIDeviceRef CreateArdaNvrhiDevice(
        nvrhi::DeviceHandle Device,
        eastl::shared_ptr<void> BackendLifetime = {});
    [[nodiscard]] FArdaRHIFramebufferRef CreateArdaNvrhiFramebuffer(
        const FArdaRHIDeviceRef& Device,
        nvrhi::FramebufferHandle Framebuffer,
        const FArdaRHIFramebufferDesc& Desc);
}
