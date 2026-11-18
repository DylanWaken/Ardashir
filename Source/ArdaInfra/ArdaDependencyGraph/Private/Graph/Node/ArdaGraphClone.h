#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"

namespace arda
{
	eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> CloneArdaGraphConstruction(
	    const FArdaDependencyGraph::FArdaImpl& Source,
	    bool bPreservePreprocessing = true);
}
