#pragma once
#include "ArdaPixelSortNodeParameters.h"

namespace arda
{
	struct FArdaPixelSortReadbackParameters
	{
		FArdaDependencyResourceHandle mSource;
		eastl::shared_ptr<FArdaPixelSortReadback> mDestination;
	};

	class FArdaPixelSortReadbackNode final
	    : public TArdaCopyDependencyNode<FArdaPixelSortReadbackNode, FArdaPixelSortReadbackParameters>
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

		/** Map only after successful completion of the recording graph ticket. */
		static std::vector<uint32_t> ReadPixels(FArdaRHIDeviceRef Device, const FArdaPixelSortReadback& Readback);
	};
}
