#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellBuildTlasNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaCornellBuildTlasNode, CornellBuildTlas);

	FArdaDependencyNodeRequirements FArdaCornellBuildTlasNode::GetRequirements(const FArdaParameters& P)
	{
		FArdaDependencyNodeRequirements R;
		R.mFeatures.Require("ray-tracing.acceleration-structure");
		R.mFeatures.Require("ray-tracing.top-level-acceleration-structure");
		if (HasAnyFlags(P.mBuildFlags,
		    EArdaRHIAccelStructBuildFlags::AllowUpdate | EArdaRHIAccelStructBuildFlags::PerformUpdate))
		{
			R.mFeatures.Require("ray-tracing.acceleration-structure-update");
		}
		if (HasAnyFlags(P.mBuildFlags, EArdaRHIAccelStructBuildFlags::AllowCompaction))
		{
			R.mFeatures.Require("ray-tracing.acceleration-structure-compaction");
		}
		return R;
	}

	FArdaDependencyNodeMetadata FArdaCornellBuildTlasNode::GetMetadata()
	{
		return {"cornell.build_tlas", 1};
	}

	FArdaDependencyNodeDesc FArdaCornellBuildTlasNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		// Declare the native build or compaction inputs so the compiler owns synchronization.

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
