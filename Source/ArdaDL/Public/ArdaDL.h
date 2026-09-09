#pragma once

#include "RHI/ArdaRHI.h"

namespace arda
{
    /** Provides the opaque RHI device used by deep-learning workloads. */
    struct FArdaDLContext
    {
        /** The RHI device used to execute deep-learning workloads. */
        arda::FArdaRHIDeviceRef mDevice;
    };

    /** Returns the stable name of the deep-learning module. */
    [[nodiscard]] const char* GetDLModuleName() noexcept;
}
