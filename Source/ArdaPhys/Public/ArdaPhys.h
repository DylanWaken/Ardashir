#pragma once

#include <nvrhi/nvrhi.h>

namespace arda::phys
{
    struct Context
    {
        nvrhi::DeviceHandle device;
    };

    [[nodiscard]] const char* GetModuleName() noexcept;
}
