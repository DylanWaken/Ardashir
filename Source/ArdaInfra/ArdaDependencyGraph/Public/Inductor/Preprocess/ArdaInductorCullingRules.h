#pragma once
#include "Inductor/Preprocess/ArdaGraphPreprocessRule.h"

namespace arda
{
	/** Mark unobservable graph nodes as culled while retaining their declarations. */
	class FArdaCullUnreachableRule final : public TArdaGraphPreprocessRule<FArdaCullUnreachableRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaCullUnreachableRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const;
	};

	/** Clear culling metadata so every graph node remains executable. */
	class FArdaKeepAllNodesRule final : public TArdaGraphPreprocessRule<FArdaKeepAllNodesRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "FArdaKeepAllNodesRule";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const;
	};
}
