#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	class FArdaCornellCompactBlasNode final
	    : public TArdaComputeDependencyNode<FArdaCornellCompactBlasNode, FArdaCornellNodeParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FParameters& Parameters,
		    const FState& State,
		    FInstanceState& Instance);
	};
}
