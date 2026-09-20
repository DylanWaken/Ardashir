#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Validate shader and pipeline patterns for the nodes retained in the result. */
	class FArdaInferPipelinesRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaInferPipelinesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
	};
}
