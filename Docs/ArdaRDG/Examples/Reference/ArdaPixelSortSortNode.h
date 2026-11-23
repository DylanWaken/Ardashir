// Documentation snapshot of Source/ArdaTests/Examples/PixelSort/Public/Nodes/ArdaPixelSortSortNode.h.
#pragma once
#include "ArdaPixelSortNodeParameters.h"

namespace arda
{
	class FArdaPixelSortSortNode final
	    : public TArdaNodeDefinition<FArdaPixelSortSortNode, FArdaPixelSortSortParameters, EArdaNodeType::Native>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static TArdaRHIResult<EArdaNativeExecutionMode> SelectNativeExecutionMode(
		    const FArdaNativeComputeContext& Context,
		    eastl::string& ProviderId);
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaRHIStatus Validate(const FArdaParameters& Parameters);
		static TArdaRHIResult<FArdaNativeComputePlan> PrepareNative(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaPixelSortSortNode, PixelSortSort);
}
