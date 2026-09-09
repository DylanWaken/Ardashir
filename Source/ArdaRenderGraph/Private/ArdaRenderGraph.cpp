#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraph.h"

namespace arda
{
    /**
     * Returns the stable module identifier used by module discovery and diagnostics.
     * The returned string has static storage and requires no graph lifecycle state.
     */
    const char* GetRenderGraphModuleName() noexcept
    {
        return "ArdaRenderGraph";
    }
}
