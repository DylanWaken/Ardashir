#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainUploadCameraNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	FArdaRHIStatus FArdaTerrainUploadCameraNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = sizeof(FArdaTerrainCameraSettings);
		D.mStructureStride = 0;
		D.mUsage = EArdaRHIBufferUsage::Constant;
		return C.Buffer(P.mDestination, "Destination", D);
	}

	FArdaDependencyNodeMetadata FArdaTerrainUploadCameraNode::GetMetadata()
	{
		return {"example.terrain.upload-camera", 1};
	}

	eastl::string FArdaTerrainUploadCameraNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;

		Key.Resource(P.mDestination);
		Key.Value(reinterpret_cast<uintptr_t>(P.mInputs.get()));

		return Key.Build();
	}

	FArdaRHIStatus FArdaTerrainUploadCameraNode::Validate(const FArdaParameters& P)
	{
		if (!P.mInputs)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Terrain upload requires retained frame inputs.");
		}
		return {};
	}

	FArdaDependencyNodeDesc FArdaTerrainUploadCameraNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};
		return D;
	}

	FArdaRHIStatus FArdaTerrainUploadCameraNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P.mDestination),
		    &P.mInputs->mCamera,
		    sizeof(FArdaTerrainCameraSettings));
	}
}
