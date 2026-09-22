#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Validate traditional/meshlet stage dataflow and assemble each pixel terminal into one native draw. */
	class FArdaAssembleGraphicsStagesRule final : public FArdaGraphPreprocessRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaAssembleGraphicsStagesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override;
	};
}
