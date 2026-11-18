/** Expand named subgraph content before stage assembly and dependency analysis. */
#pragma once

#include "Inductor/Preprocess/ArdaGraphPreprocessRule.h"

namespace arda
{
	/** Replace subgraph interfaces with their recursively authored content in the preprocessing graph.
	 * Select before rules that assemble stages, infer resource usage or resolve resource dependencies.
	 * Repeating this rule is harmless after expansion. Omitting it is valid for graphs without subgraphs.
	 */
	class FArdaExpandSubgraphsRule final : public TArdaGraphPreprocessRule<FArdaExpandSubgraphsRule>
	{
	public:
		/** Return the stable rule name used in preprocessing diagnostics. */
		static const char* GetRuleName() noexcept
		{
			return "FArdaExpandSubgraphsRule";
		}

		/** Expansion preserves the resulting graph's eligibility for adaptive scheduling. */
		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		/** Expand content using the preprocessor's isolated working graph and authoring scope. */
		FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const;
	};
}
