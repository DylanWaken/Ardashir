#include "ArdaRHITestPch.h"
#include "Nodes/ArdaTriangleVertexUploadNode.h"

namespace arda
{
	FArdaDependencyNodeMetadata FArdaTriangleVertexUploadNode::GetMetadata()
	{
		return {"example.triangle.vertexupload", 1};
	}

	eastl::string FArdaTriangleVertexUploadNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string K;
		for (auto Value : {P.mGraph, uint64_t(P.mIndex), P.mGeneration})
		{
			K.append(reinterpret_cast<const char*>(&Value), sizeof(Value));
		}
		return K;
	}

	FArdaDependencyNodeDesc FArdaTriangleVertexUploadNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};
		D.mAccesses[0].mBufferRange = {0, sizeof(ArdaTriangleVertices)};
		return D;
	}

	FArdaRHIStatus FArdaTriangleVertexUploadNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P), ArdaTriangleVertices, sizeof(ArdaTriangleVertices));
	}
}
