#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell Accumulate node instance. */
	struct FArdaCornellAccumulateParameters
	{
		/** Input sample radiance buffer. */
		FArdaDependencyResourceHandle mRadiance;
		/** Accumulation texture updated by this node. */
		FArdaDependencyResourceHandle mAccumulation;
		/** Input frame constant buffer. */
		FArdaDependencyResourceHandle mConstants;
		/** Number of accumulation compute groups along X. */
		uint32_t mGroupCountX = 1;
		/** Number of accumulation compute groups along Y. */
		uint32_t mGroupCountY = 1;
		/** Scratch memory reserved while recording this node. */
		uint64_t mWorkspaceBytes = 0;
	};

	class FArdaCornellAccumulateNode final
	    : public TArdaComputeDependencyNode<FArdaCornellAccumulateNode, FArdaCornellAccumulateParameters>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
