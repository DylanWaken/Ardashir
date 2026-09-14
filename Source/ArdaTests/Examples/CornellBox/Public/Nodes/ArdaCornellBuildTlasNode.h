#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell BuildTlas node instance. */
	struct FArdaCornellBuildTlasParameters
	{
		/** Input bottom-level acceleration structure. */
		FArdaDependencyResourceHandle mBlas;
		/** Destination top-level acceleration structure. */
		FArdaDependencyResourceHandle mTlas;
		/** Build policy for the top-level acceleration structure. */
		EArdaRHIAccelStructBuildFlags mBuildFlags = EArdaRHIAccelStructBuildFlags::PreferFastTrace;
		/** Required acceleration-structure build scratch size in bytes. */
		uint64_t mWorkspaceBytes = 0;
	};

	class FArdaCornellBuildTlasNode final
	    : public TArdaComputeDependencyNode<FArdaCornellBuildTlasNode, FArdaCornellBuildTlasParameters>
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
