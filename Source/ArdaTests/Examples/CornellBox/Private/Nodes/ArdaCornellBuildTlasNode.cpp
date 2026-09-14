#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellBuildTlasNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	FArdaDependencyNodeRequirements FArdaCornellBuildTlasNode::GetRequirements(const FArdaParameters& P)
	{
		FArdaDependencyNodeRequirements R;
		R.mFeatures.mbRequireAccelerationStructures = true;
		R.mFeatures.mbRequireTopLevelAccelerationStructures = true;
		R.mFeatures.mbRequireAccelerationStructureUpdate = HasAnyFlags(P.mBuildFlags,
		    EArdaRHIAccelStructBuildFlags::AllowUpdate | EArdaRHIAccelStructBuildFlags::PerformUpdate);
		R.mFeatures.mbRequireAccelerationStructureCompaction =
		    HasAnyFlags(P.mBuildFlags, EArdaRHIAccelStructBuildFlags::AllowCompaction);
		return R;
	}

	FArdaDependencyNodeMetadata FArdaCornellBuildTlasNode::GetMetadata()
	{
		return {"cornell.build_tlas", 1};
	}

	eastl::string FArdaCornellBuildTlasNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mBlas);
		Key.Resource(P.mTlas);
		Key.Value(static_cast<uint64_t>(P.mBuildFlags));
		Key.Value(P.mWorkspaceBytes);
		return Key.Build();
	}

	FArdaDependencyNodeDesc FArdaCornellBuildTlasNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		// Declare the native build or compaction inputs so the compiler owns synchronization.
		D.mAccesses = {{P.mBlas, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructRead},
		    {P.mTlas, EArdaDependencyAccess::Write, EArdaRHIResourceState::AccelStructWrite}};

		// Rebuild on every graph execution, even when no other node consumes the result.
		D.mbSideEffect = true;

		return D;
	}

	FArdaRHIStatus FArdaCornellBuildTlasNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		auto& Commands = C.GetCommands();

		FArdaRHIRayTracingInstanceDesc I;
		I.mBottomLevelAccelStruct = C.GetAccelerationStructure(P.mBlas);
		return Commands.BuildTopLevelAccelStruct(*C.GetAccelerationStructure(P.mTlas), {I}, P.mBuildFlags);
	}
}
