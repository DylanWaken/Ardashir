#pragma once

#include <nvrhi/nvrhi.h>

namespace arda::phys
{
    /** Provides the NVRHI device used by physics workloads. */
    struct FArdaPhysContext
    {
        /** The NVRHI device used to execute physics workloads. */
        nvrhi::DeviceHandle mDevice;
    };

    /** Returns the stable name of the physics module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
