#include "ArdaRHITestPch.h"
#include "Nodes/ArdaTriangleIndexUploadNode.h"

namespace arda
{
	FArdaDependencyNodeMetadata FArdaTriangleIndexUploadNode::GetMetadata()
	{
		return {"example.triangle.indexupload", 1};
	}

	eastl::string FArdaTriangleIndexUploadNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string K;
		for (auto Value : {P.mGraph, uint64_t(P.mIndex), P.mGeneration})
		{
			K.append(reinterpret_cast<const char*>(&Value), sizeof(Value));
		}
		return K;
	}

	FArdaDependencyNodeDesc FArdaTriangleIndexUploadNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};
		D.mAccesses[0].mBufferRange = {0, sizeof(ArdaTriangleIndices)};
		return D;
	}

	FArdaRHIStatus FArdaTriangleIndexUploadNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P), ArdaTriangleIndices, sizeof(ArdaTriangleIndices));
	}
}
