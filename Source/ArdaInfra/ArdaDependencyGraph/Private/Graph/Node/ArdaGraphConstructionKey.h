#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"

namespace arda
{
	/** Complete semantic bytes for revision lookup; logical handle generations are excluded. */
	eastl::string BuildArdaGraphConstructionKey(const FArdaDependencyGraph::FArdaImpl& Graph);
	/** Fast lookup digest. Compare complete construction keys before reusing a revision. */
	uint64_t HashArdaGraphConstructionKey(const eastl::string& Key);
}
