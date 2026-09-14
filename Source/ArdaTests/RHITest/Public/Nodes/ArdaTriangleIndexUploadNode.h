#pragma once
#include "ArdaTriangleNodeParameters.h"

namespace arda
{
	class FArdaTriangleIndexUploadNode final
	    : public TArdaCopyDependencyNode<FArdaTriangleIndexUploadNode, FArdaTriangleIndexUploadParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
