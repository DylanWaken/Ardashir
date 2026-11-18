/** Configurable graph transformations, with separate preprocessing and compilation contracts. */
#pragma once
#include "Graph/Node/ArdaInductorGraph.h"
#include "RHI/Config/ArdaRHIStatus.h"

namespace arda
{
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

}

#include "Inductor/Preprocess/ArdaGraphPreprocessRule.h"
#include "Inductor/Compiler/ArdaGraphCompileRule.h"
