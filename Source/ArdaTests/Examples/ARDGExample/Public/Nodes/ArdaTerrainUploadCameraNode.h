#pragma once
#include "ArdaTerrainNodeParameters.h"

namespace arda
{
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTerrainUploadCameraParameters)
		ARDA_NODE_BUFFER(mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
		ARDA_NODE_PARAMETER(eastl::shared_ptr<FArdaTerrainFrameInputs>, mInputs)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Self-contained operation; invocation registers and prepares this node's implementation. */
	class FArdaTerrainUploadCameraNode final : public TArdaNodeDefinition<FArdaTerrainUploadCameraNode,
	                                               FArdaTerrainUploadCameraParameters,
	                                               EArdaNodeType::Copy>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaRHIStatus Validate(const FArdaParameters& Parameters);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaTerrainUploadCameraNode, TerrainUploadCamera);
}
