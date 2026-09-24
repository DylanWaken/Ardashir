#pragma once
#include "Graph/Node/ArdaDependencyGraph.h"
#include "Graph/Node/ArdaDependencyNodeDesc.h"

namespace arda
{
	void InferArdaNodeRequirements(const FArdaDependencyGraph::FArdaImpl& Graph,
	    EArdaDependencyNodeKind Kind,
	    FArdaDependencyNodeDesc& Desc);

	/** Rechecks every attached node without GPU setup; null devices check explicit requirements only. */
	[[nodiscard]] FArdaRHIStatus CheckArdaGraphRequiredFeatures(const FArdaDependencyGraph::FArdaImpl& Graph);
}
