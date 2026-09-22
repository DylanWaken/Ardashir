#include "Inductor/Config/ArdaInductorRuleSet.h"
#include "Inductor/Rules/ArdaInductorRules.h"

namespace arda
{
	FArdaInductorRuleSet::FArdaInductorRuleSet()
	{
		// Immutable defaults share identity across independently constructed option values.
		static const eastl::vector<eastl::shared_ptr<const FArdaGraphPreprocessRule>> Preprocess = {
		    MakeArdaGraphRule<FArdaAssembleGraphicsStagesRule>(),
		    MakeArdaGraphRule<FArdaAssembleRayTracingStagesRule>(),
		    MakeArdaGraphRule<FArdaDeduceResourceUsageRule>(),
		    MakeArdaGraphRule<FArdaValidateResourceAccessesRule>(),
		    MakeArdaGraphRule<FArdaResolveDependenciesRule>(),
		    MakeArdaGraphRule<FArdaValidateAcyclicRule>()};
		static const eastl::vector<eastl::shared_ptr<const FArdaGraphCompileRule>> Compile = {
		    MakeArdaGraphRule<FArdaFlattenGraphRule>(),
		    MakeArdaGraphRule<FArdaCullUnreachableRule>(),
		    MakeArdaGraphRule<FArdaInferPipelinesRule>(),
		    MakeArdaGraphRule<FArdaSearchOrderRule>(),
		    MakeArdaGraphRule<FArdaPlanMemoryRule>(),
		    MakeArdaGraphRule<FArdaAssignQueuesRule>(),
		    MakeArdaGraphRule<FArdaBatchCudaRule>(),
		    MakeArdaGraphRule<FArdaFlattenDispatchesRule>(),
		    MakeArdaGraphRule<FArdaResolveFencesRule>()};
		mPreprocessRules = Preprocess;
		mCompileRules = Compile;
	}

	FArdaRHIStatus ValidateArdaInductorRules(const FArdaInductorRuleSet& Rules)
	{
		const auto Validate = [](const auto& List) -> FArdaRHIStatus
		{
			for (const auto& Rule : List)
			{
				if (!Rule || !Rule->GetName() || !*Rule->GetName())
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "Rule lists require non-null named instances.");
				}
			}
			return {};
		};
		if (auto S = Validate(Rules.mPreprocessRules); !S)
		{
			return S;
		}
		return Validate(Rules.mCompileRules);
	}
}
