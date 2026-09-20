/** Pipeline inference and native cache resolution entry points. */
#pragma once
#include "Inductor/Rules/ArdaInductorPipeline.h"

namespace arda
{

	/** Infers one unique compatible pipeline without allocating a native PSO.
     * Missing dependencies, dependency cycles, ambiguous stages/groups, and shaders without
     * deterministic content hashes return errors. The input graph and resources are retained
     * only through shader/layout references copied into the returned pattern.
     */
	[[nodiscard]] TArdaRHIResult<FArdaInductorPipelinePattern> InferArdaInductorPipeline(
	    const eastl::vector<FArdaInductorPipelineNode>& Nodes,
	    const FArdaInductorPipelineRequest& Request);

	/** Creates or reuses the inferred pipeline through the existing device-bound PSO cache.
     * A framebuffer is required for incomplete graphics/meshlet attachment formats or samples.
     * Unsupported device/backend features propagate the cache's ordinary status.
     */
	[[nodiscard]] TArdaRHIResult<FArdaInductorResolvedPipeline> ResolveArdaInductorPipeline(
	    FArdaPipelineStateCache& Cache,
	    const FArdaInductorPipelinePattern& Pattern,
	    const FArdaRHIFramebufferRef& Framebuffer = {});

}
