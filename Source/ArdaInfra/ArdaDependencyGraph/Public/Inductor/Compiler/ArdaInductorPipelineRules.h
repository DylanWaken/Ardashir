#pragma once
#include "Inductor/Compiler/ArdaGraphCompileRule.h"

namespace arda
{
	/** Validate shader and pipeline patterns for the nodes retained in the result. */
	class FArdaInferPipelinesRule final : public TArdaGraphCompileRule<FArdaInferPipelinesRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaInferPipelinesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Compile(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const;
	};
}
