#pragma once

#include <nvrhi/nvrhi.h>

namespace arda::dl
{
    struct Context
    {
        nvrhi::DeviceHandle device;
    };

    [[nodiscard]] const char* GetModuleName() noexcept;
}
