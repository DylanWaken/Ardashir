#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellBuildTlasNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	FArdaDependencyNodeMetadata FArdaCornellBuildTlasNode::GetMetadata()
	{
		return {"cornell.build_tlas", 1};
	}

	eastl::string FArdaCornellBuildTlasNode::GetCanonicalKey(const FParameters& P)
	{
		return MakeCornellNodeKey(P);
	}

	FArdaDependencyNodeDesc FArdaCornellBuildTlasNode::Describe(const FParameters& P, const FState& Prepared)
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
		D.mbSideEffect = true;

		return D;
	}

	FArdaRHIStatus FArdaCornellBuildTlasNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		auto& Commands = C.GetCommands();

		FArdaRHIRayTracingInstanceDesc I;
		I.mBottomLevelAccelStruct = C.GetAccelerationStructure(P.mResources[0]);
		return Commands.BuildTopLevelAccelStruct(*C.GetAccelerationStructure(P.mResources[1]), {I}, P.mBuildFlags);
	}
}
