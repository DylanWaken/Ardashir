#pragma once

#include "RHI/ArdaRHI.h"

namespace arda
{
    /** Provides the opaque RHI device used by global-illumination workloads. */
    struct FArdaGIContext
    {
        /** The RHI device used to execute global-illumination workloads. */
        arda::FArdaRHIDeviceRef mDevice;
    };

    /** Returns the stable name of the global-illumination module. */
    [[nodiscard]] const char* GetGIModuleName() noexcept;
}
