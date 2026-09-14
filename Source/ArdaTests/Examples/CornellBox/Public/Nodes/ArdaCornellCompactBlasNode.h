#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell CompactBlas node instance. */
	struct FArdaCornellCompactBlasParameters
	{
		/** Input bottom-level acceleration structure. */
		FArdaDependencyResourceHandle mSource;
		/** Destination compacted bottom-level acceleration structure. */
		FArdaDependencyResourceHandle mDestination;
		/** Scratch memory reserved while recording this node. */
		uint64_t mWorkspaceBytes = 0;
	};

	class FArdaCornellCompactBlasNode final
	    : public TArdaComputeDependencyNode<FArdaCornellCompactBlasNode, FArdaCornellCompactBlasParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Hardware admission is checked before output declaration or device preparation. */
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
