#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"

namespace arda
{
	/** Validates authored shader arguments and derives layout/resource effects. */
	FArdaRHIStatus DescribeArdaShaderBindings(FArdaDependencyGraph::FArdaImpl& Graph, FArdaDependencyNodeDesc& Desc);
	/** Derives physical effects without a pipeline or native layout, for checking declared stage outputs. */
	FArdaRHIStatus DescribeArdaShaderAccesses(const FArdaDependencyGraph::FArdaImpl& Graph,
	    FArdaDependencyNodeDesc& Desc);
}
