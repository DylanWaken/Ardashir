#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Plan allocation lifetimes and alias dependencies for the current node order. */
	class FArdaPlanMemoryRule final : public FArdaGraphCompileRule
	{
	public:
		/** Permit extra ordering edges to reuse storage, even without a global memory objective or cap. */
		explicit FArdaPlanMemoryRule(bool AllowSerialization = false)
		    : mbAllowSerialization(AllowSerialization)
		{
		}

		const char* GetName() const noexcept override
		{
			return "FArdaPlanMemoryRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
		bool mbAllowSerialization;
	};
}
