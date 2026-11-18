#pragma once
#include "Inductor/Preprocess/ArdaGraphPreprocessRule.h"

namespace arda
{
	/** Validate explicit ray-shader values and merge their stages and bindings into each ray-generation dispatch. */
	class FArdaAssembleRayTracingStagesRule final : public TArdaGraphPreprocessRule<FArdaAssembleRayTracingStagesRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaAssembleRayTracingStagesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const;
	};
}
