#include "ArdaRHITestPch.h"
#include "Nodes/ArdaTriangleVertexUploadNode.h"

namespace arda
{
	FArdaDependencyNodeMetadata FArdaTriangleVertexUploadNode::GetMetadata()
	{
		return {"example.triangle.vertexupload", 1};
	}

	eastl::string FArdaTriangleVertexUploadNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder().Resource(P.mVertices).Build();
	}

	FArdaDependencyNodeDesc FArdaTriangleVertexUploadNode::Describe(const FArdaParameters& P,
	    const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mVertices, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};
		D.mAccesses[0].mBufferRange = {0, sizeof(ArdaTriangleVertices)};
		return D;
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
