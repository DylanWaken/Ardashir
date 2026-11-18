#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell CompactBlas node instance. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCornellCompactBlasParameters)
		/** Input bottom-level acceleration structure. */
		ARDA_NODE_ACCELERATION_STRUCTURE(mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructRead)
		/** Destination compacted bottom-level acceleration structure. */
		ARDA_NODE_ACCELERATION_STRUCTURE(mDestination,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::AccelStructWrite)
		/** Scratch memory reserved while recording this node. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mWorkspaceBytes, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaCornellCompactBlasNode final : public TArdaNodeDefinition<FArdaCornellCompactBlasNode,
	                                              FArdaCornellCompactBlasParameters,
	                                              EArdaNodeType::Compute>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Hardware admission is checked before output declaration or device preparation. */
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCornellCompactBlasNode, CornellCompactBlas);
}
