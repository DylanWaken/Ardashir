#pragma once
#include "ArdaPixelSortNodeParameters.h"

namespace arda
{
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaPixelSortReadbackParameters)
		ARDA_NODE_TEXTURE(mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource)
		ARDA_NODE_PARAMETER(eastl::shared_ptr<FArdaPixelSortReadback>, mDestination)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaPixelSortReadbackNode final
	    : public TArdaNodeDefinition<FArdaPixelSortReadbackNode, FArdaPixelSortReadbackParameters, EArdaNodeType::Copy>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaPixelSortReadbackNode, PixelSortReadback);
}
