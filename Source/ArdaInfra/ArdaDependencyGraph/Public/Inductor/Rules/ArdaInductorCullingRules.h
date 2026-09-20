#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Remove unobservable nodes from the existing execution order; keep graph declarations intact. */
	class FArdaCullUnreachableRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaCullUnreachableRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
	};

	/** Restore every graph node in topological order and clear culling output. */
	class FArdaKeepAllNodesRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaKeepAllNodesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
	};
}
