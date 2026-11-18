#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"

namespace arda
{
	/** Copies immutable semantic and scheduling inputs without active frame ownership. */
	eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> CloneArdaInductorTimingInputs(
	    const FArdaDependencyGraph::FArdaImpl& Graph);
	/** Estimate per-node costs from the newest available samples. A batch duration is distributed
	 * in proportion to current node costs; raw telemetry remains an aggregate measured duration.
	 */
	bool ApplyArdaInductorTimingCosts(FArdaDependencyGraph::FArdaImpl& Graph,
	    const eastl::vector<FArdaInductorTimingSample>& Samples,
	    uint32_t MinimumSamples);
	void AdvanceArdaInductorTelemetry(FArdaDependencyGraph::FArdaImpl& Graph);
	FArdaRHIStatus RequestArdaInductorTimingOptimization(FArdaDependencyGraph::FArdaImpl& Graph, bool Automatic);
	void CancelArdaInductorTimingOptimization(FArdaDependencyGraph::FArdaImpl& Graph);
}
