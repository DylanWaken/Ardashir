#pragma once

#include <nvrhi/nvrhi.h>

namespace arda::render_graph
{
    /** Provides the NVRHI device used to execute render graphs. */
    struct FArdaRenderGraphContext
    {
        /** The NVRHI device on which render-graph work is executed. */
        nvrhi::DeviceHandle mDevice;
    };

    /** Returns the stable name of the render-graph module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
