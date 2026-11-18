#pragma once
#include "Inductor/Preprocess/ArdaGraphPreprocessRule.h"

namespace arda
{
	/** Validate traditional/meshlet stage dataflow and assemble each pixel terminal into one native draw. */
	class FArdaAssembleGraphicsStagesRule final : public TArdaGraphPreprocessRule<FArdaAssembleGraphicsStagesRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaAssembleGraphicsStagesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const;
	};
}
