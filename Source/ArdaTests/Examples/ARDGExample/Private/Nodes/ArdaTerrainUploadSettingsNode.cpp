#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainUploadSettingsNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	FArdaRHIStatus FArdaTerrainUploadSettingsNode::DeclareResources(FArdaDependencyResourceContext& C,
	    FArdaParameters& P)
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = sizeof(FArdaTerrainSettings);
		D.mStructureStride = sizeof(FArdaTerrainSettings);
		D.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource;
		return C.Buffer(P.mDestination, "Destination", D);
	}

	FArdaDependencyNodeMetadata FArdaTerrainUploadSettingsNode::GetMetadata()
	{
		return {"example.terrain.upload-settings", 1};
	}

	eastl::string FArdaTerrainUploadSettingsNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;

		Key.Resource(P.mDestination);
		Key.Value(reinterpret_cast<uintptr_t>(P.mInputs.get()));

		return Key.Build();
	}

	FArdaRHIStatus FArdaTerrainUploadSettingsNode::Validate(const FArdaParameters& P)
	{
		if (!P.mInputs)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Terrain upload requires retained frame inputs.");
		}
		return {};
	}

	FArdaDependencyNodeDesc FArdaTerrainUploadSettingsNode::Describe(const FArdaParameters& P,
	    const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};
		return D;
	}

	FArdaRHIStatus FArdaTerrainUploadSettingsNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P.mDestination),
		    &P.mInputs->mSettings,
		    sizeof(FArdaTerrainSettings));
	}
}
