/** Automatic instancing candidates produced after pipeline batching. */
#pragma once

#include "Inductor/Compiler/ArdaGraphCompileRule.h"

namespace arda
{
	/** Combine eligible adjacent traditional draws after FArdaBatchPipelinesRule.
	 * All stages must declare ContiguousInstances. Shader bindings and attachments must match;
	 * nonattachment writes remain separate. Dynamic geometry/ranges receive a final runtime check.
	 */
	class FArdaMergeDrawInstancesRule final : public TArdaGraphCompileRule<FArdaMergeDrawInstancesRule>
	{
	public:
		/** Stable rule identity for configuration and diagnostics. */
		static const char* GetRuleName() noexcept
		{
			return "FArdaMergeDrawInstancesRule";
		}

		/** Candidate grouping does not change the dependency order or resource lifetime proof. */
		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		/** Rebuild candidate groups from the supplied pipeline batches without removing logical nodes. */
		FArdaRHIStatus Compile(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const;
	};
}
