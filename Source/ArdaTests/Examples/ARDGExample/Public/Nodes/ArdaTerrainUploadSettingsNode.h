#pragma once
#include "ArdaTerrainNodeParameters.h"

namespace arda
{
	struct FArdaTerrainUploadSettingsParameters
	{
		FArdaDependencyResourceHandle mDestination;
		eastl::shared_ptr<FArdaTerrainFrameInputs> mInputs;
	};

	/** Self-contained operation; attachment registers and prepares only this node's implementation. */
	class FArdaTerrainUploadSettingsNode final
	    : public TArdaCopyDependencyNode<FArdaTerrainUploadSettingsNode, FArdaTerrainUploadSettingsParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static FArdaRHIStatus Validate(const FParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FParameters& Parameters,
		    const FState& State,
		    FInstanceState& Instance);
	};
}
