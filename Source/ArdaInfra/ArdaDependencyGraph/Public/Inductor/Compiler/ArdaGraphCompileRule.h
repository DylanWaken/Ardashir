/** Rules that read a preprocessed graph and produce an execution plan. */
#pragma once

#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	struct FArdaInductorCompileResult;

	/** Compilation never edits the input graph. Each rule refines the unpublished execution result. */
	class FArdaGraphCompileRule : public FArdaGraphRule
	{
	public:
		/** Refine the unpublished execution result while preserving graph declarations. */
		virtual FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const = 0;
	};

	/** A configured compiler rule implements GetRuleName() and Compile(Graph, Result) const. */
	template <class Derived>
	class TArdaGraphCompileRule : public FArdaGraphCompileRule
	{
	public:
		/** Return the derived rule's stable diagnostic name. */
		const char* GetName() const noexcept override
		{
			return Derived::GetRuleName();
		}

		/** Forward result generation to the configured derived rule. */
		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const override
		{
			return static_cast<const Derived&>(*this).Compile(Graph, Result);
		}
	};
}
