#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellBuildBlasNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaCornellBuildBlasNode, CornellBuildBlas);

	FArdaDependencyNodeRequirements FArdaCornellBuildBlasNode::GetRequirements(const FArdaParameters& P)
	{
		FArdaDependencyNodeRequirements R;
		R.mFeatures.Require("ray-tracing.acceleration-structure");
		R.mFeatures.Require("ray-tracing.bottom-level-acceleration-structure");
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

	FArdaDependencyNodeMetadata FArdaCornellBuildBlasNode::GetMetadata()
	{
		return {"cornell.build_blas", 1};
	}

	FArdaDependencyNodeDesc FArdaCornellBuildBlasNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		// Declare the native build or compaction inputs so the compiler owns synchronization.

		return D;
	}

	FArdaRHIStatus FArdaCornellBuildBlasNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		auto& Commands = C.GetCommands();

		FArdaRHIRayTracingGeometryDesc G;
		G.mType = EArdaRHIRayTracingGeometryType::Triangles;
		G.mFlags = EArdaRHIRayTracingGeometryFlags::Opaque;
		G.mVertexOrAABBBuffer = C.GetBuffer(P.mVertices);
		G.mIndexBuffer = C.GetBuffer(P.mIndices);
		G.mVertexFormat = EArdaRHIFormat::RGB32Float;
		G.mIndexFormat = EArdaRHIFormat::R32UInt;
		G.mVertexOrAABBCount = P.mVertexCount;
		G.mIndexCount = P.mIndexCount;
		G.mStride = P.mVertexStride;
		return Commands.BuildBottomLevelAccelStruct(*C.GetAccelerationStructure(P.mBlas), {G}, P.mBuildFlags);
	}
}
