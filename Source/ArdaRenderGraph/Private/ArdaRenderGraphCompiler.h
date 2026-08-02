#pragma once

#include "ArdaRenderGraphBuilder.h"

namespace arda::render_graph
{
    /**
     * Runs the device-independent compilation stages for a completed render graph.
     *
     * Compilation validates declarations, anchors observable outputs at the
     * epilogue, removes dead passes, selects queue pipelines, lowers resource
     * states to transitions, and builds lifetime and synchronization metadata.
     * It does not allocate resources, record commands, or submit GPU work.
     */
    class FARDGCompiler final
    {
    public:
        /**
         * Compiles Builder once and returns its stable compile products.
         *
         * The first call mutates Builder's private pass/resource metadata and
         * appends the graph epilogue. Later calls return the cached result. On
         * success, execution order remains registration order with dead passes
         * removed, and Builder is marked compiled only after every stage passes.
         *
         * @param Builder The fully declared graph to compile.
         * @return Builder-owned compilation metadata, valid for Builder's lifetime.
         */
        [[nodiscard]] static const FARDGCompileResult& Compile(FARDGBuilder& Builder);
    };
}
