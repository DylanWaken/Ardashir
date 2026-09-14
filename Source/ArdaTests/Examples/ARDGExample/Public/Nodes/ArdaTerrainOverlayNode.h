#pragma once
#include "ArdaTerrainNodeParameters.h"

namespace arda
{
	struct FArdaTerrainOverlayParameters
	{
		FArdaDependencyResourceHandle mSource;
		FArdaDependencyResourceHandle mColor;
		uint32_t mWidth = 0;
		uint32_t mHeight = 0;
	};

	/** Self-contained operation; attachment registers and prepares only this node's implementation. */
	class FArdaTerrainOverlayNode final
	    : public TArdaGraphicsDependencyNode<FArdaTerrainOverlayNode, FArdaTerrainOverlayParameters>
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
