#pragma once
#include "ArdaTerrainNodeParameters.h"

namespace arda
{
	struct FArdaTerrainUploadCameraParameters
	{
		FArdaDependencyResourceHandle mDestination;
		eastl::shared_ptr<FArdaTerrainFrameInputs> mInputs;
	};

	/** Self-contained operation; attachment registers and prepares only this node's implementation. */
	class FArdaTerrainUploadCameraNode final
	    : public TArdaCopyDependencyNode<FArdaTerrainUploadCameraNode, FArdaTerrainUploadCameraParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaRHIStatus Validate(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
