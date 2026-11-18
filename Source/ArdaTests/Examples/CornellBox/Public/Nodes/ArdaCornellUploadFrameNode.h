#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell UploadFrame node instance. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCornellUploadFrameParameters)
		/** Output constant buffer; leave empty for graph allocation. */
		ARDA_NODE_BUFFER(mConstants, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
		/** Retained frame input; update only before synchronous graph execution. */
		ARDA_NODE_PARAMETER(eastl::shared_ptr<FArdaCornellFrameInput>, mFrame)
		/** Scratch memory reserved while recording this node. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mWorkspaceBytes, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaCornellUploadFrameNode final : public TArdaNodeDefinition<FArdaCornellUploadFrameNode,
	                                              FArdaCornellUploadFrameParameters,
	                                              EArdaNodeType::Copy>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaRHIStatus Validate(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCornellUploadFrameNode, CornellUploadFrame);
}
