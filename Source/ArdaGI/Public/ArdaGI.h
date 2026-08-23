#pragma once

#include "RHI/ArdaRHI.h"

namespace arda::gi
{
    /** Provides the opaque RHI device used by global-illumination workloads. */
    struct FArdaGIContext
    {
        /** The RHI device used to execute global-illumination workloads. */
        rhi::FArdaRHIDeviceRef mDevice;
    };

    /** Returns the stable name of the global-illumination module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
