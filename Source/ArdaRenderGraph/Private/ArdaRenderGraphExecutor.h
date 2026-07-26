#pragma once

#include "ArdaRenderGraphBuilder.h"

namespace arda::render_graph
{
    /** Materializes, records, and submits one compiled render graph. */
    class FARDGExecutor final
    {
    public:
        /** Executes Builder once and returns its stable submission report. */
        [[nodiscard]] static const FARDGExecutionResult& Execute(
            FARDGBuilder& Builder,
            const FARDGExecuteOptions& Options);
    };
}
