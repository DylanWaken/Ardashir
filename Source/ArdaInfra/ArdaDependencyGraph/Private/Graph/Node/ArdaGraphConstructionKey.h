#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"

namespace arda
{
	/** Normalize resource and stage handles while a node describes this construction. */
	class FArdaConstructionKeyScope final
	{
	public:
		explicit FArdaConstructionKeyScope(const FArdaDependencyGraph::FArdaImpl& Graph) noexcept;
		~FArdaConstructionKeyScope();
		FArdaConstructionKeyScope(const FArdaConstructionKeyScope&) = delete;
		FArdaConstructionKeyScope& operator=(const FArdaConstructionKeyScope&) = delete;

	private:
		const FArdaDependencyGraph::FArdaImpl* mPrevious;
	};

	/** Complete semantic bytes for revision lookup; logical handle generations are excluded. */
	eastl::string BuildArdaGraphConstructionKey(const FArdaDependencyGraph::FArdaImpl& Graph);
	/** Fast lookup digest. Compare complete construction keys before reusing a revision. */
	uint64_t HashArdaGraphConstructionKey(const eastl::string& Key);
}
