#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell UploadFrame node instance. */
	struct FArdaCornellUploadFrameParameters
	{
		/** Output constant buffer; leave empty for graph allocation. */
		FArdaDependencyResourceHandle mConstants;
		/** Retained frame input; update only before synchronous graph execution. */
		eastl::shared_ptr<FArdaCornellFrameInput> mFrame;
		/** Scratch memory reserved while recording this node. */
		uint64_t mWorkspaceBytes = 0;
	};

	class FArdaCornellUploadFrameNode final
	    : public TArdaCopyDependencyNode<FArdaCornellUploadFrameNode, FArdaCornellUploadFrameParameters>
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
}
