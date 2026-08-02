#pragma once

#include "ArdaRenderGraphBuilder.h"

namespace arda::render_graph
{
    /**
     * Validates render-graph declarations and compiler-generated state metadata.
     *
     * The pre-compile stage rejects malformed ownership, access, extraction,
     * and production declarations before culling can hide them. The later
     * transition stage independently replays compiler output to verify that
     * every live access and graph-exit state is satisfied.
     */
    class FARDGValidation final
    {
    public:
        /**
         * Validates all build-time declarations before compilation mutates the graph.
         *
         * This stage checks every resource and pass, including passes that may
         * later be culled. It verifies flags, ownership/backing consistency,
         * legal states and ranges, descriptor capabilities, extraction records,
         * and registration-order production before reads. It emits no compile
         * products and does not alter Graph.
         *
         * @param Graph The completed build-time graph representation.
         */
        static void ValidateBeforeCompile(const FARDGBuilder::FImpl& Graph);

        /**
         * Replays compiled transitions and verifies the lowered resource state machine.
         *
         * Starting from each resource's graph-entry state, this post-barrier
         * stage checks transition continuity, confirms each live pass sees its
         * normalized required state, and ensures used external or extracted
         * resources finish in their requested final state. It is an independent
         * consistency check of compiler output and does not mutate Graph.
         *
         * @param Graph A graph whose barriers and execution order are compiled.
         */
        static void ValidateTransitions(const FARDGBuilder::FImpl& Graph);
    };
}
