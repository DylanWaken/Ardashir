#pragma once
#include "Inductor/Compiler/ArdaGraphCompileRule.h"

namespace arda
{
	/** Batch adjacent compatible pipelines without reordering operations or hiding resource barriers. */
	class FArdaBatchPipelinesRule final : public TArdaGraphCompileRule<FArdaBatchPipelinesRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaBatchPipelinesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Compile(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const;
	};
}
