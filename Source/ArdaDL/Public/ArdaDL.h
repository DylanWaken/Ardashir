#pragma once

#include "RHI/ArdaRHI.h"

namespace arda::dl
{
    /** Provides the opaque RHI device used by deep-learning workloads. */
    struct FArdaDLContext
    {
        /** The RHI device used to execute deep-learning workloads. */
        rhi::FArdaRHIDeviceRef mDevice;
    };

    /** Returns the stable name of the deep-learning module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
