#pragma once
#include "ArdaTriangleNodeParameters.h"

namespace arda
{
	class FArdaTriangleIndexUploadNode final : public TArdaNodeDefinition<FArdaTriangleIndexUploadNode,
	                                               FArdaTriangleIndexUploadParameters,
	                                               EArdaNodeType::Copy>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaTriangleIndexUploadNode, TriangleIndexUpload);
}
