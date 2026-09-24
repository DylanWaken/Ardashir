#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Reject missing backend or environment requirements for every attached node, including culled work.
	 * Rechecks frozen public parameters without preparing nodes or issuing GPU commands.
	 * The compiler also enforces this invariant when custom rule lists omit this default rule.
	 */
	class FArdaCheckRequiredFeaturesRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaCheckRequiredFeaturesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
	};
}
