#pragma once
#include "ArdaPixelSortNodeParameters.h"

namespace arda
{
	class FArdaPixelSortNoiseNode final
	    : public TArdaNodeDefinition<FArdaPixelSortNoiseNode, FArdaPixelSortNoiseParameters, EArdaNodeType::Compute>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static FArdaRHIStatus Validate(const FArdaParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaPixelSortNoiseNode, PixelSortNoise);
}
