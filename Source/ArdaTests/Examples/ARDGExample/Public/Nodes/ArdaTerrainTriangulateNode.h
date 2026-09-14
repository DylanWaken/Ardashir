#pragma once
#include "ArdaTerrainNodeParameters.h"

namespace arda
{
	struct FArdaTerrainTriangulateParameters
	{
		FArdaDependencyResourceHandle mHeightmap;
		FArdaDependencyResourceHandle mVertices;
		FArdaDependencyResourceHandle mIndices;
	};

	/** Self-contained operation; attachment registers and prepares only this node's implementation. */
	class FArdaTerrainTriangulateNode final
	    : public TArdaComputeDependencyNode<FArdaTerrainTriangulateNode, FArdaTerrainTriangulateParameters>
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
