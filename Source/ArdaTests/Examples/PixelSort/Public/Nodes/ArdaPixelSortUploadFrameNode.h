#pragma once
#include "ArdaPixelSortNodeParameters.h"

namespace arda
{
	class FArdaPixelSortUploadFrameNode final
	    : public TArdaCopyDependencyNode<FArdaPixelSortUploadFrameNode, FArdaPixelSortNodeParameters>
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
