#include "ArdaRHITestPch.h"
#include "Nodes/ArdaTriangleVertexUploadNode.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaTriangleVertexUploadNode, TriangleVertexUpload);

	FArdaDependencyNodeMetadata FArdaTriangleVertexUploadNode::GetMetadata()
	{
		return {"example.triangle.vertexupload", 1};
	}

	FArdaRHIStatus FArdaTriangleVertexUploadNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P.mVertices),
		    ArdaTriangleVertices,
		    sizeof(ArdaTriangleVertices));
	}
}
