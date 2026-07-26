#pragma once

#include <nvrhi/nvrhi.h>

namespace arda::dl
{
    /** Provides the NVRHI device used by deep-learning workloads. */
    struct FArdaDLContext
    {
        /** The NVRHI device used to execute deep-learning workloads. */
        nvrhi::DeviceHandle device;
    };

    /** Returns the stable name of the deep-learning module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
