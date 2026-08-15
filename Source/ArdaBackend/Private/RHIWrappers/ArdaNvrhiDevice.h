/** @file ArdaNvrhiDevice.h
 *  Declares private Arda RHI facades over NVRHI devices and framebuffers.
 */
#pragma once

#include "RHIWrappers/ArdaRHI.h"

#include <EASTL/shared_ptr.h>
#include <nvrhi/nvrhi.h>

namespace arda::rhi::private_impl
{
    /** Creates the opaque Arda facade for an existing NVRHI device.
     *  @param Device NVRHI device wrapped by the facade.
     *  @param BackendLifetime Optional shared owner that keeps backend state alive.
     *  @return Reference to the created Arda RHI device facade.
     */
    [[nodiscard]] FArdaRHIDeviceRef CreateArdaNvrhiDevice(
        nvrhi::DeviceHandle Device,
        eastl::shared_ptr<void> BackendLifetime = {});

    /** Creates an Arda framebuffer facade for an existing NVRHI framebuffer.
     *  @param Device Arda RHI device associated with the framebuffer.
     *  @param Framebuffer NVRHI framebuffer wrapped by the facade.
     *  @param Desc Arda description of the framebuffer attachments.
     *  @return Reference to the created Arda RHI framebuffer facade.
     */
    [[nodiscard]] FArdaRHIFramebufferRef CreateArdaNvrhiFramebuffer(
        const FArdaRHIDeviceRef& Device,
        nvrhi::FramebufferHandle Framebuffer,
        const FArdaRHIFramebufferDesc& Desc);
}
