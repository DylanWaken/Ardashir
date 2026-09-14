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
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaRHIStatus Validate(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);

		/** Map only after successful completion of the recording graph ticket. */
		static std::vector<uint32_t> ReadPixels(FArdaRHIDeviceRef Device, const FArdaPixelSortReadback& Readback);
	};
}
