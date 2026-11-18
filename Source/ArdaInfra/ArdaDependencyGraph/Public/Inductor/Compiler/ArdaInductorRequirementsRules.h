#pragma once
#include "Inductor/Compiler/ArdaGraphCompileRule.h"

namespace arda
{
	/** Reject missing backend or environment requirements for every attached node, including culled work.
	 * Rechecks frozen public parameters without preparing nodes or issuing GPU commands.
	 * The compiler also enforces this invariant when custom rule lists omit this default rule.
	 */
	class FArdaCheckRequiredFeaturesRule final : public TArdaGraphCompileRule<FArdaCheckRequiredFeaturesRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaCheckRequiredFeaturesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Compile(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const;
	};
}
