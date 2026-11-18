/** Private authoring adapter for the subgraph expansion preprocessing rule. */
#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"

namespace arda
{
	class FArdaSubgraphPreprocessor final
	{
	public:
		static FArdaRHIStatus Expand(FArdaDependencyGraph& Graph);
	};
}
