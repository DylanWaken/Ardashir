#pragma once
#include "Graph/Node/ArdaDependencyGraph.h"
#include "Graph/Node/ArdaDependencyNodeDesc.h"

namespace arda
{
	void InferArdaNodeRequirements(const FArdaDependencyGraph::FArdaImpl& Graph,
	    EArdaDependencyNodeKind Kind,
	    FArdaDependencyNodeDesc& Desc);
}
