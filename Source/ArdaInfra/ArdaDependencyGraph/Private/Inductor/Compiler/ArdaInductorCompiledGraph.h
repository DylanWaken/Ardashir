#pragma once

#include "Graph/Node/ArdaDependencyGraphImpl.h"

namespace arda
{
	/** Owned compiler input without any execution result; prevents result/runtime ownership cycles. */
	struct FArdaInductorCompiledGraph
	{
		eastl::shared_ptr<const FArdaDependencyGraph::FArdaImpl> mGraph;
	};
}
