#pragma once
#include "ArdaDependencyNode.h"

namespace arda
{
	struct FArdaComputeWriteParameters
	{
		FArdaDependencyResourceHandle mOutput;
		uint32_t mValue = 0;
	};

	class FArdaComputeWriteNode final
	    : public TArdaComputeDependencyNode<FArdaComputeWriteNode, FArdaComputeWriteParameters>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static eastl::string GetCanonicalKey(const FArdaParameters& P);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
