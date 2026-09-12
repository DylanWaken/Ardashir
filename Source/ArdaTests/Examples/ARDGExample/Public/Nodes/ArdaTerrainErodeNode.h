#pragma once
#include "ArdaTerrainNodeParameters.h"

namespace arda
{
	struct FArdaTerrainErodeParameters
	{
		FArdaDependencyResourceHandle mSource;
		FArdaDependencyResourceHandle mHeightmap;
	};

	/** Self-contained operation; attachment registers and prepares only this node's implementation. */
	class FArdaTerrainErodeNode final
	    : public TArdaComputeDependencyNode<FArdaTerrainErodeNode, FArdaTerrainErodeParameters>
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
