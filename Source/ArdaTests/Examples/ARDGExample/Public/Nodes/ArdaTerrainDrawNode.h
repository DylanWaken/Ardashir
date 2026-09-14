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
		EArdaRHIFormat mColorFormat = EArdaRHIFormat::RGBA8UNorm;
	};

	/** Self-contained operation; attachment registers and prepares only this node's implementation. */
	class FArdaTerrainDrawNode final
	    : public TArdaGraphicsDependencyNode<FArdaTerrainDrawNode, FArdaTerrainDrawParameters>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
