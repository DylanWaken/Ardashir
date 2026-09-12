#pragma once
#include "ArdaTerrainNodeParameters.h"

namespace arda
{
	struct FArdaTerrainDrawParameters
	{
		FArdaDependencyResourceHandle mHeightmap;
		FArdaDependencyResourceHandle mVertices;
		FArdaDependencyResourceHandle mIndices;
		FArdaDependencyResourceHandle mCamera;
		FArdaDependencyResourceHandle mColor;
		FArdaDependencyResourceHandle mDepth;
		uint32_t mWidth = 0;
		uint32_t mHeight = 0;
	};

	/** Self-contained operation; attachment registers and prepares only this node's implementation. */
	class FArdaTerrainDrawNode final
	    : public TArdaGraphicsDependencyNode<FArdaTerrainDrawNode, FArdaTerrainDrawParameters>
	{
	public:
		struct FState;
		struct FInstanceState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FState>> Prepare(FArdaRHIDeviceRef Device);
		static TArdaRHIResult<eastl::shared_ptr<FInstanceState>> CreateInstance(FArdaRHIDeviceRef Device,
		    const FParameters& Parameters,
		    const FState& State);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FParameters& Parameters,
		    const FState& State,
		    FInstanceState& Instance);
	};
}
