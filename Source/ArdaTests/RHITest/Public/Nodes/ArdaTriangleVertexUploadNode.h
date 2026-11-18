#pragma once
#include "ArdaTriangleNodeParameters.h"

namespace arda
{
	class FArdaTriangleVertexUploadNode final : public TArdaNodeDefinition<FArdaTriangleVertexUploadNode,
	                                                FArdaTriangleVertexUploadParameters,
	                                                EArdaNodeType::Copy>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaTriangleVertexUploadNode, TriangleVertexUpload);
}
