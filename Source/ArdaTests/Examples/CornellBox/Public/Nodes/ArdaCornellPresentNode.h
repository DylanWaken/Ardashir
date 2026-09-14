#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell Present node instance. */
	struct FArdaCornellPresentParameters
	{
		/** Input accumulated radiance texture. */
		FArdaDependencyResourceHandle mAccumulation;
		/** Input frame constant buffer. */
		FArdaDependencyResourceHandle mConstants;
		/** Output render-target texture. */
		FArdaDependencyResourceHandle mColor;
		/** Viewport and scissor width in pixels. */
		uint32_t mWidth = 1;
		/** Viewport and scissor height in pixels. */
		uint32_t mHeight = 1;
		/** Scratch memory reserved while recording this node. */
		uint64_t mWorkspaceBytes = 0;
	};

	class FArdaCornellPresentNode final
	    : public TArdaGraphicsDependencyNode<FArdaCornellPresentNode, FArdaCornellPresentParameters>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
