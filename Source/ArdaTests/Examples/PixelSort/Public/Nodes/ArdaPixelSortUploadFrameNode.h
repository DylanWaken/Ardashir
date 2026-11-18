#pragma once
#include "ArdaPixelSortNodeParameters.h"

namespace arda
{
	class FArdaPixelSortUploadFrameNode final : public TArdaNodeDefinition<FArdaPixelSortUploadFrameNode,
	                                                FArdaPixelSortUploadFrameParameters,
	                                                EArdaNodeType::Copy>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaRHIStatus Validate(const FArdaParameters& Parameters);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaPixelSortUploadFrameNode, PixelSortUploadFrame);
}
