#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraph.h"

namespace arda::render_graph
{
    /**
     * Returns the stable module identifier used by module discovery and diagnostics.
     * The returned string has static storage and requires no graph lifecycle state.
     */
    const char* GetModuleName() noexcept
    {
        return "ArdaRenderGraph";
    }
}
