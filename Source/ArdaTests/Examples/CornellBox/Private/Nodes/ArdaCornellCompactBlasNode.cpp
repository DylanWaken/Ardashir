#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellCompactBlasNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	FArdaDependencyNodeMetadata FArdaCornellCompactBlasNode::GetMetadata()
	{
		return {"cornell.compact_blas", 1};
	}

	eastl::string FArdaCornellCompactBlasNode::GetCanonicalKey(const FParameters& P)
	{
		return MakeCornellNodeKey(P);
	}

	FArdaDependencyNodeDesc FArdaCornellCompactBlasNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		const auto Read = [&](uint32_t I, EArdaRHIResourceState State)
		{
			D.mAccesses.push_back({P.mResources[I], EArdaDependencyAccess::Read, State});
		};
		const auto Write = [&](uint32_t I, EArdaRHIResourceState State)
		{
			D.mAccesses.push_back({P.mResources[I], EArdaDependencyAccess::Write, State});
		};
		Read(0, EArdaRHIResourceState::AccelStructRead);
		Write(1, EArdaRHIResourceState::AccelStructWrite);

		return D;
	}

	FArdaRHIStatus FArdaCornellCompactBlasNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		auto& Commands = C.GetCommands();

		return Commands.CompactAccelStruct(*C.GetAccelerationStructure(P.mResources[1]),
		    *C.GetAccelerationStructure(P.mResources[0]));
	}
}
