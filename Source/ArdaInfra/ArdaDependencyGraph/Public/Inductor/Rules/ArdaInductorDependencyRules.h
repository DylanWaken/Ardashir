#pragma once
#include "Inductor/Rules/ArdaGraphRule.h"

namespace arda
{
	/** Validate logical accesses and framebuffer declarations. */
	class FArdaValidateResourceAccessesRule final : public FArdaGraphPreprocessRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaValidateResourceAccessesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override;
	};

	/** Rebuild resource hazards while retaining existing graph ordering edges. */
	class FArdaResolveDependenciesRule final : public FArdaGraphPreprocessRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaResolveDependenciesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override;
	};

	/** Reject cycles in the preprocessed graph. */
	class FArdaValidateAcyclicRule final : public FArdaGraphPreprocessRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "FArdaValidateAcyclicRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override;
	};
}
