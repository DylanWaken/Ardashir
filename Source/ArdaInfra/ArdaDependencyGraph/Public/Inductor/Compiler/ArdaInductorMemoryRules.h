#pragma once
#include "Inductor/Compiler/ArdaGraphCompileRule.h"

namespace arda
{
	/** Plan allocation lifetimes and alias dependencies for the current node order. */
	class FArdaPlanMemoryRule final : public TArdaGraphCompileRule<FArdaPlanMemoryRule>
	{
	public:
		/** Permit extra ordering edges to reuse storage, even without a global memory objective or cap. */
		explicit FArdaPlanMemoryRule(bool AllowSerialization = false)
		    : mbAllowSerialization(AllowSerialization)
		{
		}

		static const char* GetRuleName() noexcept
		{
			return "FArdaPlanMemoryRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Compile(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const;
		bool mbAllowSerialization;
	};
}
