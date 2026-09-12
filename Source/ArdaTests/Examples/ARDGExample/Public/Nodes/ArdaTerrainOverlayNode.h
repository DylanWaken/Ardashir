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
		struct FState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FParameters& Parameters,
		    const FState& State,
		    FInstanceState& Instance);
	};
}
