/** Build result-owned execution state from immutable graph declarations. */
#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"

namespace arda
{
	FArdaRHIStatus InitializeArdaInductorNodes(FArdaDependencyGraph::FArdaImpl& Graph);
}
