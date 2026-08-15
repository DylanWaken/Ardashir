#pragma once

#include "RHIWrappers/ArdaRHI.h"

namespace arda::phys
{
    /** Provides the opaque RHI device used by physics workloads. */
    struct FArdaPhysContext
    {
        /** The RHI device used to execute physics workloads. */
        rhi::FArdaRHIDeviceRef mDevice;
    };

    /** Returns the stable name of the physics module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
