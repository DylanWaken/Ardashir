#include "ArdaRHITestPch.h"
#include "Nodes/ArdaTriangleIndexUploadNode.h"

namespace arda
{
	FArdaDependencyNodeMetadata FArdaTriangleIndexUploadNode::GetMetadata()
	{
		return {"example.triangle.indexupload", 1};
	}

	eastl::string FArdaTriangleIndexUploadNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder().Resource(P.mIndices).Build();
	}

	FArdaDependencyNodeDesc FArdaTriangleIndexUploadNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mIndices, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};
		D.mAccesses[0].mBufferRange = {0, sizeof(ArdaTriangleIndices)};
		return D;
	}

	FArdaRHIStatus FArdaTriangleIndexUploadNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P.mIndices), ArdaTriangleIndices, sizeof(ArdaTriangleIndices));
	}
}
