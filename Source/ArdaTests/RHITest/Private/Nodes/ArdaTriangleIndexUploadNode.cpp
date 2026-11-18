#include "ArdaRHITestPch.h"
#include "Nodes/ArdaTriangleIndexUploadNode.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaTriangleIndexUploadNode, TriangleIndexUpload);

	FArdaDependencyNodeMetadata FArdaTriangleIndexUploadNode::GetMetadata()
	{
		return {"example.triangle.indexupload", 1};
	}

	FArdaRHIStatus FArdaTriangleIndexUploadNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P.mIndices), ArdaTriangleIndices, sizeof(ArdaTriangleIndices));
	}
}
