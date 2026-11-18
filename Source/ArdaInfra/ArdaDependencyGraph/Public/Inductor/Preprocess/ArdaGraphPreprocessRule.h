/** Graph transformations applied to a private normal graph before compilation. */
#pragma once

#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Edit graph topology, declarations and culling metadata. No GPU work may be submitted. */
	class FArdaGraphPreprocessRule : public FArdaGraphRule
	{
	public:
		/** Transform the unpublished graph; a failure discards the preprocessing output. */
		virtual FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const = 0;
	};

	/** Named preprocessing rule. Derived implements GetRuleName and Preprocess(Graph). */
	template <class Derived>
	class TArdaGraphPreprocessRule : public FArdaGraphPreprocessRule
	{
	public:
		/** Return the derived rule's stable diagnostic name. */
		const char* GetName() const noexcept final
		{
			return Derived::GetRuleName();
		}

		/** Forward the graph transformation to the configured derived rule. */
		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const final
		{
			return static_cast<const Derived*>(this)->Preprocess(Graph);
		}
	};
}
