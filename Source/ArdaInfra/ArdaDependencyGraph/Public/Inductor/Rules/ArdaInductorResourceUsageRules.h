#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Resolve AutoDeduction from all declared accesses, bindings and attachments before memory planning.
	 * Explicit flags are preserved and checked; AutoDeduction may be combined with additional usage hints.
	 * Imported resources retain their native capabilities and cannot acquire new usage flags.
	 */
	class FArdaDeduceResourceUsageRule final : public FArdaGraphPreprocessRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaDeduceResourceUsageRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override;
	};
}
