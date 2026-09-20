#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Seed the compilation result with a topological node order. */
	class FArdaFlattenGraphRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaFlattenGraphRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
	};

	/** Group contiguous executable CUDA nodes without changing node or queue order. */
	class FArdaBatchCudaRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaBatchCudaRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
	};

	/** Flatten native queue work and CUDA batches into dispatches and stream lists. */
	class FArdaFlattenDispatchesRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaFlattenDispatchesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
	};

	/** Derive dispatch dependencies and cross-domain fence edges from graph and memory dependencies. */
	class FArdaResolveFencesRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaResolveFencesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override;
	};
}
