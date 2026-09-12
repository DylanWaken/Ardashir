#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellBuildBlasNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	FArdaDependencyNodeMetadata FArdaCornellBuildBlasNode::GetMetadata()
	{
		return {"cornell.build_blas", 1};
	}

	eastl::string FArdaCornellBuildBlasNode::GetCanonicalKey(const FParameters& P)
	{
		return MakeCornellNodeKey(P);
	}

	FArdaDependencyNodeDesc FArdaCornellBuildBlasNode::Describe(const FParameters& P, const FState& Prepared)
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
		Read(0, EArdaRHIResourceState::AccelStructBuildInput);
		Read(1, EArdaRHIResourceState::AccelStructBuildInput);
		Write(2, EArdaRHIResourceState::AccelStructWrite);

		return D;
	}

	FArdaRHIStatus FArdaCornellBuildBlasNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		auto& Commands = C.GetCommands();

		FArdaRHIRayTracingGeometryDesc G;
		G.mType = EArdaRHIRayTracingGeometryType::Triangles;
		G.mFlags = EArdaRHIRayTracingGeometryFlags::Opaque;
		G.mVertexOrAABBBuffer = C.GetBuffer(P.mResources[0]);
		G.mIndexBuffer = C.GetBuffer(P.mResources[1]);
		G.mVertexFormat = EArdaRHIFormat::RGB32Float;
		G.mIndexFormat = EArdaRHIFormat::R32UInt;
		G.mVertexOrAABBCount = P.mVertexCount;
		G.mIndexCount = P.mIndexCount;
		G.mStride = P.mVertexStride;
		return Commands.BuildBottomLevelAccelStruct(*C.GetAccelerationStructure(P.mResources[2]), {G}, P.mBuildFlags);
	}
}
