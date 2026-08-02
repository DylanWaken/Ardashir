#pragma once

#include "ArdaRenderGraphBuilder.h"

namespace arda::render_graph
{
    /** Implements the materialize, record, and CPU-submit stages of one graph. */
    class FARDGExecutor final
    {
    public:
        /**
         * Compiles if needed, materializes resources, records command lists,
         * inserts queue waits, and submits Builder exactly once.
         *
         * The returned report is stored by Builder and remains stable for its
         * lifetime. Success means CPU submission has completed; this function
         * does not wait for the GPU to finish.
         */
        [[nodiscard]] static const FARDGExecutionResult& Execute(
            FARDGBuilder& Builder,
            const FARDGExecuteOptions& Options);
    };
}
