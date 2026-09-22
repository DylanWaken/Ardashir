#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Validate explicit ray-shader values and merge their stages and bindings into each ray-generation dispatch. */
	class FArdaAssembleRayTracingStagesRule final : public FArdaGraphPreprocessRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaAssembleRayTracingStagesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override;
	};
}
