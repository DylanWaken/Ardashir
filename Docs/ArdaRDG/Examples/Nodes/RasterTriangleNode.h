#pragma once
#include "ArdaDependencyNode.h"

namespace arda
{
	struct FArdaRasterTriangleParameters
	{
		FArdaDependencyResourceHandle mColor;
		uint32_t mWidth = 64, mHeight = 64;
	};

	class FArdaRasterTriangleNode final
	    : public TArdaGraphicsDependencyNode<FArdaRasterTriangleNode, FArdaRasterTriangleParameters>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static eastl::string GetCanonicalKey(const FArdaParameters& P);
		static FArdaRHIStatus Validate(const FArdaParameters& P);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
