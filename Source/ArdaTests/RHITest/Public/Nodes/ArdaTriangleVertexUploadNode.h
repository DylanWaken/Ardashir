#pragma once
#include "ArdaTriangleNodeParameters.h"

namespace arda
{
	class FArdaTriangleVertexUploadNode final
	    : public TArdaCopyDependencyNode<FArdaTriangleVertexUploadNode, FArdaTriangleVertexUploadParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaTriangleVertexUploadNode, TriangleVertexUpload);
}
