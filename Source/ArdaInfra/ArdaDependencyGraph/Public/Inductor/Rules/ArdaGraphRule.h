/** Configurable graph transformations, with separate preprocessing and compilation contracts. */
#pragma once
#include "Graph/Node/ArdaInductorGraph.h"
#include "RHI/Config/ArdaRHIStatus.h"

namespace arda
{
	struct FArdaInductorCompileResult;

	/** Shared parent for all rules. Instances own configuration; keep it immutable once selected
     * by graph options. Store application scratch in local variables or the pending result.
     */
	class FArdaGraphRule
	{
	public:
		virtual ~FArdaGraphRule() = default;
		virtual const char* GetName() const noexcept = 0;

		virtual bool AllowsAdaptiveScheduling() const noexcept
		{
			return false;
		}
	};

	/** Transform the working graph before any compile rule runs. No GPU work may be submitted. */
	class FArdaGraphPreprocessRule : public FArdaGraphRule
	{
	public:
		virtual FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const = 0;
	};

	/** Modify the current compilation result. Rules may be inserted, omitted or repeated;
     * validate required input data and preserve graph dependencies. Failures prevent publication.
     */
	class FArdaGraphCompileRule : public FArdaGraphRule
	{
	public:
		virtual FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const = 0;
	};
}
