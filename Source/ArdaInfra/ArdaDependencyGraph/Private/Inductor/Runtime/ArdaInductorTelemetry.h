#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"

namespace arda
{
	/** Copies immutable semantic and scheduling inputs without active frame ownership. */
	eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> CloneArdaInductorTimingInputs(
	    const FArdaDependencyGraph::FArdaImpl& Graph);
	void AdvanceArdaInductorTelemetry(FArdaDependencyGraph::FArdaImpl& Graph);
	FArdaRHIStatus RequestArdaInductorTimingOptimization(FArdaDependencyGraph::FArdaImpl& Graph, bool Automatic);
	void CancelArdaInductorTimingOptimization(FArdaDependencyGraph::FArdaImpl& Graph);
}
