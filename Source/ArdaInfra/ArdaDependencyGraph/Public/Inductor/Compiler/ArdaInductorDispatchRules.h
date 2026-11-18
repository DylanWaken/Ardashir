#pragma once
#include "Inductor/Compiler/ArdaGraphCompileRule.h"

namespace arda
{
	/** Seed the compilation result with a topological node order. */
	class FArdaFlattenGraphRule final : public TArdaGraphCompileRule<FArdaFlattenGraphRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaFlattenGraphRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Compile(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const;
	};

	/** Group contiguous executable CUDA nodes without changing node or queue order. */
	class FArdaBatchCudaRule final : public TArdaGraphCompileRule<FArdaBatchCudaRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaBatchCudaRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Compile(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const;
	};

	/** Flatten native queue work and CUDA batches into dispatches and stream lists. */
	class FArdaFlattenDispatchesRule final : public TArdaGraphCompileRule<FArdaFlattenDispatchesRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaFlattenDispatchesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Compile(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const;
	};

	/** Derive dispatch dependencies and cross-domain fence edges from graph and memory dependencies. */
	class FArdaResolveFencesRule final : public TArdaGraphCompileRule<FArdaResolveFencesRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaResolveFencesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Compile(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const;
	};
}
