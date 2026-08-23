#pragma once

#include "RHI/ArdaRHIProvider.h"

namespace arda::rhi::provider
{
    /** Constructs ArdaBackend's RHI facade around one backend-provider device. */
    [[nodiscard]] FArdaRHIDeviceRef CreateArdaRHIDevice(
        eastl::shared_ptr<IArdaRHIProviderDevice> Device);
}
