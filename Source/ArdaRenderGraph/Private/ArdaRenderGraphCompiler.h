#pragma once

#include "ArdaRenderGraphBuilder.h"

namespace arda::render_graph
{
    /** Produces deterministic device-independent scheduling metadata. */
    class FARDGCompiler final
    {
    public:
        /** Compiles Builder and returns its stable compile products. */
        [[nodiscard]] static const FARDGCompileResult& Compile(FARDGBuilder& Builder);
    };
}
