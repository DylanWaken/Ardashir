#pragma once

#include <nvrhi/nvrhi.h>

namespace arda::gi
{
    /** Provides the NVRHI device used by global-illumination workloads. */
    struct FArdaGIContext
    {
        /** The NVRHI device used to execute global-illumination workloads. */
        nvrhi::DeviceHandle device;
    };

    /** Returns the stable name of the global-illumination module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
