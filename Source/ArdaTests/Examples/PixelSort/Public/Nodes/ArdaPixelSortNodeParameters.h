#pragma once

#include "ArdaDependencyNode.h"

#include <filesystem>
#include <vector>

namespace arda
{
	/** Values sampled while recording a frame; update before Submit on the render thread. */
	struct FArdaPixelSortFrameInput
	{
		float mTime = 0.f;
		uint32_t mChannel = 0;
		uint32_t mThreshold = 48;
		bool mbOriginal = false;
	};

	/** Logical graph inputs; each node privately retains its device setup. */
	struct FArdaPixelSortNodeParameters
	{
		FArdaDependencyResourceHandle mConstants;
		FArdaDependencyResourceHandle mNoise;
		FArdaDependencyResourceHandle mSorted;
		FArdaDependencyResourceHandle mColor;
		eastl::shared_ptr<FArdaPixelSortFrameInput> mInput = eastl::make_shared<FArdaPixelSortFrameInput>();
		uint32_t mWidth = 0;
		uint32_t mHeight = 0;
	};

	/** Optional CPU readback storage. ReadPixels requires successful completion of its graph ticket. */
	struct FArdaPixelSortReadback
	{
		FArdaRHIStagingTextureRef mStaging;
	};

}
