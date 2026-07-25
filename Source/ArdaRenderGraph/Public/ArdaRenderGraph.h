#pragma once

#include <nvrhi/nvrhi.h>

namespace arda::render_graph
{
    struct Context
    {
        nvrhi::DeviceHandle device;
    };

    [[nodiscard]] const char* GetModuleName() noexcept;
}
