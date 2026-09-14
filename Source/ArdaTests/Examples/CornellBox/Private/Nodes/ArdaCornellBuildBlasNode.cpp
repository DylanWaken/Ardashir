#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellBuildBlasNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	FArdaDependencyNodeRequirements FArdaCornellBuildBlasNode::GetRequirements(const FArdaParameters& P)
	{
		FArdaDependencyNodeRequirements R;
		R.mFeatures.mbRequireAccelerationStructures = true;
		R.mFeatures.mbRequireBottomLevelAccelerationStructures = true;
		R.mFeatures.mbRequireAccelerationStructureUpdate = HasAnyFlags(P.mBuildFlags,
		    EArdaRHIAccelStructBuildFlags::AllowUpdate | EArdaRHIAccelStructBuildFlags::PerformUpdate);
		R.mFeatures.mbRequireAccelerationStructureCompaction =
		    HasAnyFlags(P.mBuildFlags, EArdaRHIAccelStructBuildFlags::AllowCompaction);
		return R;
	}

	FArdaDependencyNodeMetadata FArdaCornellBuildBlasNode::GetMetadata()
	{
		return {"cornell.build_blas", 1};
	}

	eastl::string FArdaCornellBuildBlasNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mVertices);
		Key.Resource(P.mIndices);
		Key.Resource(P.mBlas);
		Key.Value(P.mVertexCount);
		Key.Value(P.mIndexCount);
		Key.Value(P.mVertexStride);
		Key.Value(static_cast<uint64_t>(P.mBuildFlags));
		Key.Value(P.mWorkspaceBytes);
		return Key.Build();
	}

	FArdaDependencyNodeDesc FArdaCornellBuildBlasNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		// Declare the native build or compaction inputs so the compiler owns synchronization.
		D.mAccesses = {{P.mVertices, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructBuildInput},
		    {P.mIndices, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructBuildInput},
		    {P.mBlas, EArdaDependencyAccess::Write, EArdaRHIResourceState::AccelStructWrite}};

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
