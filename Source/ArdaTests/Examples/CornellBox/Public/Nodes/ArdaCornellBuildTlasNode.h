#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell BuildTlas node instance. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCornellBuildTlasParameters)
		/** Input bottom-level acceleration structure. */
		ARDA_NODE_ACCELERATION_STRUCTURE(mBlas, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructRead)
		/** Destination top-level acceleration structure. */
		ARDA_NODE_ACCELERATION_STRUCTURE(mTlas, EArdaDependencyAccess::Write, EArdaRHIResourceState::AccelStructWrite)
		/** Build policy for the top-level acceleration structure. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(EArdaRHIAccelStructBuildFlags,
		    mBuildFlags,
		    EArdaRHIAccelStructBuildFlags::PreferFastTrace)
		/** Required acceleration-structure build scratch size in bytes. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mWorkspaceBytes, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaCornellBuildTlasNode final
	    : public TArdaNodeDefinition<FArdaCornellBuildTlasNode, FArdaCornellBuildTlasParameters, EArdaNodeType::Compute>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaCornellBuildTlasNode, CornellBuildTlas);
}
