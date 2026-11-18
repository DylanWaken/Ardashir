#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"

namespace arda
{
	/** Resolve post-expansion resource aliases and deduplicate equal revisions within each edit. */
	void NormalizeArdaGraphEditRevisions(FArdaDependencyGraph::FArdaImpl& Graph);

	/** Order edit groups by shared-resource versions; report contradictory constraints as fatal errors.
	 * Only attachment order changes, and only after a complete ordering has been validated.
	 */
	void OrderArdaGraphEdits(FArdaDependencyGraph::FArdaImpl& Graph);
}
