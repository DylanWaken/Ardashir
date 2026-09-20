#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"

namespace arda
{
	/** Validates authored shader arguments and derives layout/resource effects. */
	FArdaRHIStatus DescribeArdaShaderBindings(FArdaDependencyGraph::FArdaImpl& Graph, FArdaDependencyNodeDesc& Desc);
}
