#pragma once
#include "Inductor/Preprocess/ArdaGraphPreprocessRule.h"

namespace arda
{
	/** Validate logical accesses and framebuffer declarations. */
	class FArdaValidateResourceAccessesRule final : public TArdaGraphPreprocessRule<FArdaValidateResourceAccessesRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaValidateResourceAccessesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const;
	};

	/** Rebuild resource hazards while retaining existing graph ordering edges. */
	class FArdaResolveDependenciesRule final : public TArdaGraphPreprocessRule<FArdaResolveDependenciesRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaResolveDependenciesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const;
	};

	/** Reject cycles in the preprocessed graph. */
	class FArdaValidateAcyclicRule final : public TArdaGraphPreprocessRule<FArdaValidateAcyclicRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaValidateAcyclicRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const;
	};
}
