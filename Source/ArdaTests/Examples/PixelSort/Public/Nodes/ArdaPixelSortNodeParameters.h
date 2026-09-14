#pragma once

#include "ArdaDependencyNode.h"

#include <filesystem>
#include <vector>

namespace arda
{
	/** Upload values sampled while recording; update before Submit on the render thread. */
	struct FArdaPixelSortUploadFrameInput
	{
		/** Animation time written to the constant buffer. */
		float mTime = 0.f;
		/** Selects the original image in the presentation shader. */
		bool mbOriginal = false;
	};

	/** Sort values sampled while preparing CUDA work; update before Submit on the render thread. */
	struct FArdaPixelSortSortInput
	{
		/** Color channel used as the sorting key. */
		uint32_t mChannel = 0;
		/** Minimum luminance included in a sorted run. */
		uint32_t mThreshold = 48;
	};

	/** Inputs retained only by the frame-constant upload node. */
	struct FArdaPixelSortUploadFrameParameters
	{
		/** Constant buffer output; attachment creates it when unset. */
		FArdaDependencyResourceHandle mConstants;
		/** Dynamic upload values owned by this node instance and its renderer. */
		eastl::shared_ptr<FArdaPixelSortUploadFrameInput> mInput = eastl::make_shared<FArdaPixelSortUploadFrameInput>();
		/** Image width written to the constant buffer. */
		uint32_t mWidth = 0;
		/** Image height written to the constant buffer. */
		uint32_t mHeight = 0;
	};

	/** Inputs retained only by the noise generation node. */
	struct FArdaPixelSortNoiseParameters
	{
		/** Frame constants read by the compute shader. */
		FArdaDependencyResourceHandle mConstants;
		/** Noise texture output; attachment creates it when unset. */
		FArdaDependencyResourceHandle mNoise;
		/** Width of the generated noise texture. */
		uint32_t mWidth = 0;
		/** Height of the generated noise texture. */
		uint32_t mHeight = 0;
	};

	/** Inputs retained only by the CUDA sort node. */
	struct FArdaPixelSortSortParameters
	{
		/** Noise texture read by the sort operand. */
		FArdaDependencyResourceHandle mNoise;
		/** Sorted texture output; attachment creates it when unset. */
		FArdaDependencyResourceHandle mSorted;
		/** Dynamic sorting values owned by this node instance and its renderer. */
		eastl::shared_ptr<FArdaPixelSortSortInput> mInput = eastl::make_shared<FArdaPixelSortSortInput>();
		/** Width of the input and output textures. */
		uint32_t mWidth = 0;
		/** Height of the input and output textures. */
		uint32_t mHeight = 0;
	};

	/** Inputs retained only by the presentation node. */
	struct FArdaPixelSortPresentParameters
	{
		/** Frame constants read by the pixel shader. */
		FArdaDependencyResourceHandle mConstants;
		/** Original noise image available for display. */
		FArdaDependencyResourceHandle mNoise;
		/** Sorted image available for display. */
		FArdaDependencyResourceHandle mSorted;
		/** Acquired swap-chain color target. */
		FArdaDependencyResourceHandle mColor;
		/** Viewport and scissor width. */
		uint32_t mWidth = 0;
		/** Viewport and scissor height. */
		uint32_t mHeight = 0;
	};

	/** Optional CPU readback storage. ReadPixels requires successful completion of its graph ticket. */
	struct FArdaPixelSortReadback
	{
		/** Staging texture populated by the readback node. */
		FArdaRHIStagingTextureRef mStaging;
	};

}
