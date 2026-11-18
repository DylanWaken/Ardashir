#pragma once
#include "ArdaPixelSortNodeParameters.h"

namespace arda
{
	class FArdaPixelSortPresentNode final : public TArdaNodeDefinition<FArdaPixelSortPresentNode,
	                                            FArdaPixelSortPresentParameters,
	                                            EArdaNodeType::Graphics>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus Validate(const FArdaParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaPixelSortPresentNode, PixelSortPresent);
}
