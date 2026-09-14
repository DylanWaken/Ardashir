#pragma once

#include "ArdaDependencyGraphInternal.h"

namespace arda
{
	/** Add value and overwrite edges to a topology with the same nodes as Graph.
	 * Repair/removal analysis skips malformed declarations and uninitialized-read diagnostics.
	 */
	FArdaRHIStatus ResolveArdaDependencyResourceEdges(const FArdaDependencyGraph::FArdaImpl& Graph,
	    FArdaDependencyTopology& Topology,
	    bool bValidate = true);
}
