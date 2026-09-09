#pragma once

#include "RHI/ArdaRHI.h"

namespace arda
{
    /** Provides the opaque RHI device used by physics workloads. */
    struct FArdaPhysContext
    {
        /** The RHI device used to execute physics workloads. */
        arda::FArdaRHIDeviceRef mDevice;
    };

    /** Returns the stable name of the physics module. */
    [[nodiscard]] const char* GetPhysModuleName() noexcept;
}
