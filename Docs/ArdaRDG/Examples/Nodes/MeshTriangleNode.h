#pragma once
#include "ArdaDependencyNode.h"

namespace arda
{
	struct FArdaMeshTriangleParameters
	{
		FArdaDependencyResourceHandle mColor;
		uint32_t mWidth = 64, mHeight = 64;
	};

	class FArdaMeshTriangleNode final
	    : public TArdaGraphicsDependencyNode<FArdaMeshTriangleNode, FArdaMeshTriangleParameters>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Hardware admission is checked before output declaration or device preparation. */
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
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
