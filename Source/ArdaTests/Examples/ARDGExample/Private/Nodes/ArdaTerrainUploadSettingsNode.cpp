#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainUploadSettingsNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	FArdaDependencyNodeMetadata FArdaTerrainUploadSettingsNode::GetMetadata()
	{
		return {"example.terrain.upload-settings", 1};
	}

	eastl::string FArdaTerrainUploadSettingsNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string Key;

		AppendResource(Key, P.mDestination);
		Append(Key, reinterpret_cast<uintptr_t>(P.mInputs.get()));

		return Key;
	}

	FArdaRHIStatus FArdaTerrainUploadSettingsNode::Validate(const FParameters& P)
	{
		if (!P.mInputs)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Terrain upload requires retained frame inputs.");
		}
		return {};
	}

	FArdaDependencyNodeDesc FArdaTerrainUploadSettingsNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};
		return D;
	}

	FArdaRHIStatus FArdaTerrainUploadSettingsNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P.mDestination),
		    &P.mInputs->mSettings,
		    sizeof(FArdaTerrainSettings));
	}
}
