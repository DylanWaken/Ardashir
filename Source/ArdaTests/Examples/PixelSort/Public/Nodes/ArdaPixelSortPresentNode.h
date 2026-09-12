#pragma once
#include "ArdaPixelSortNodeParameters.h"

namespace arda
{
	class FArdaPixelSortPresentNode final
	    : public TArdaGraphicsDependencyNode<FArdaPixelSortPresentNode, FArdaPixelSortNodeParameters>
	{
	public:
		struct FState;
		struct FInstanceState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static FArdaRHIStatus Validate(const FParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FState>> Prepare(FArdaRHIDeviceRef Device);
		static TArdaRHIResult<eastl::shared_ptr<FInstanceState>> CreateInstance(FArdaRHIDeviceRef Device,
		    const FParameters& Parameters,
		    const FState& State);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FParameters& Parameters,
		    const FState& State,
		    FInstanceState& Instance);
	};
}
