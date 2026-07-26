#pragma once

#include "ArdaRenderGraphBuilder.h"

namespace arda::render_graph
{
    /** Centralizes device-independent graph validation and barrier simulation. */
    class FARDGValidation final
    {
    public:
        /** Validates graph declarations before compiler products are generated. */
        static void ValidateBeforeCompile(const FARDGBuilder::FImpl& Graph);

        /** Simulates compiled transitions and verifies their state continuity. */
        static void ValidateTransitions(const FARDGBuilder::FImpl& Graph);
    };
}
