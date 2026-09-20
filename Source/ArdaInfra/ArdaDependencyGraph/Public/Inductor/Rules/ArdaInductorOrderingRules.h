#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Optimize the existing node selection using the graph search options; change ordering only. */
	class FArdaSearchOrderRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaSearchOrderRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
	};

	/** Select the alphabetically first ready node; change ordering only. */
	class FArdaStableOrderRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaStableOrderRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return false;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
	};

	/** Reorder ready nodes to reduce simultaneously live storage while preserving dependencies. */
	class FArdaMinimizeMemoryRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaMinimizeMemoryRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return false;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;

		explicit FArdaMinimizeMemoryRule(uint32_t SearchBudget = 10000)
		    : mSearchBudget(SearchBudget)
		{
		}

		uint32_t mSearchBudget;
	};

	struct FArdaInductorOrderingNode
	{
		uint32_t mNodeId = 0;
		eastl::string mName;
		double mEstimatedCost = 0;
		eastl::vector<uint32_t> mDependencies;
	};

	/** Reusable adapter that validates a custom ordering policy's permutation. */
	class FArdaInductorOrderingRule : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "CustomOrder";
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const final;
		virtual TArdaRHIResult<eastl::vector<uint32_t>> Order(const FArdaInductorGraph& Graph,
		    const eastl::vector<FArdaInductorOrderingNode>& Nodes) const = 0;
	};

	/** Policy owns its configuration and implements non-static Order(Graph, Nodes) const. */
	template <class Policy>
	class TArdaInductorOrderingRule final : public FArdaInductorOrderingRule
	{
	public:
		explicit TArdaInductorOrderingRule(Policy Value = {})
		    : mPolicy(eastl::move(Value))
		{
		}

		TArdaRHIResult<eastl::vector<uint32_t>> Order(const FArdaInductorGraph& Graph,
		    const eastl::vector<FArdaInductorOrderingNode>& Nodes) const override
		{
			return mPolicy.Order(Graph, Nodes);
		}

		Policy mPolicy;
	};
}
