/** Ordered rule instances selected during graph setup. */
#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace arda
{
	/** Construct a configured rule instance. Retained instances are immutable and may be shared
     * by snapshots. Reuse an instance for the same configuration; replace it when settings change.
     */
	template <class Rule, class... Args>
	eastl::shared_ptr<const Rule> MakeArdaGraphRule(Args&&... Arguments)
	{
		static_assert(eastl::is_base_of_v<FArdaGraphRule, Rule>, "Rules must derive from FArdaGraphRule.");
		return eastl::make_shared<const Rule>(eastl::forward<Args>(Arguments)...);
	}

	/** Both lists run in their given order. No fixed slots, rule count, stage IDs or completion bits.
     * Rule identity and list order participate in revision caching. Configuration must not be
     * changed through an external mutable alias while any graph or snapshot retains the instance.
     */
	struct FArdaInductorRuleSet
	{
		FArdaInductorRuleSet();
		eastl::vector<eastl::shared_ptr<const FArdaGraphPreprocessRule>> mPreprocessRules;
		eastl::vector<eastl::shared_ptr<const FArdaGraphCompileRule>> mCompileRules;
	};

	FArdaRHIStatus ValidateArdaInductorRules(const FArdaInductorRuleSet& Rules);
}
