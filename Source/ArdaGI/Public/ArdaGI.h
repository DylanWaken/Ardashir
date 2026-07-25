#pragma once

#include <nvrhi/nvrhi.h>

namespace arda::gi
{
    struct Context
    {
        nvrhi::DeviceHandle device;
    };

    [[nodiscard]] const char* GetModuleName() noexcept;
}
