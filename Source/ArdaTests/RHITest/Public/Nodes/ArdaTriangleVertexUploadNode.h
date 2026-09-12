#pragma once
#include "ArdaTriangleNodeParameters.h"

namespace arda
{
	class FArdaTriangleVertexUploadNode final
	    : public TArdaCopyDependencyNode<FArdaTriangleVertexUploadNode, FArdaDependencyResourceHandle>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FParameters& Parameters,
		    const FState& State,
		    FInstanceState& Instance);
	};
}
