#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainUploadCameraNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	FArdaDependencyNodeMetadata FArdaTerrainUploadCameraNode::GetMetadata()
	{
		return {"example.terrain.upload-camera", 1};
	}

	eastl::string FArdaTerrainUploadCameraNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string Key;

		AppendResource(Key, P.mDestination);
		Append(Key, reinterpret_cast<uintptr_t>(P.mInputs.get()));

		return Key;
	}

	FArdaRHIStatus FArdaTerrainUploadCameraNode::Validate(const FParameters& P)
	{
		if (!P.mInputs)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Terrain upload requires retained frame inputs.");
		}
		return {};
	}

	FArdaDependencyNodeDesc FArdaTerrainUploadCameraNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};
		return D;
	}

	FArdaRHIStatus FArdaTerrainUploadCameraNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P.mDestination),
		    &P.mInputs->mCamera,
		    sizeof(FArdaTerrainCameraSettings));
	}
}
