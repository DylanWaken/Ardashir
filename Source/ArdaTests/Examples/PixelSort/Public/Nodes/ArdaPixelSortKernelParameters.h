#pragma once

#include <cstdint>

namespace arda
{
	inline constexpr uint32_t ArdaPixelSortItemsPerThread = 4;
	inline constexpr uint32_t ArdaPixelSortRowThreads = 128;
	inline constexpr uint32_t ArdaPixelSortColumnThreads = 256;

	struct FArdaPixelSortShaderConstants
	{
		uint32_t mWidth;
		uint32_t mHeight;
		uint32_t mChannel;
		uint32_t mThreshold;
	};

	// Mirrors the two surface handles and four scalars of each exported CUDA entry.
	// The provider fills the handles from the invocation's texture views.
	struct FArdaPixelSortKernelParameters
	{
		uint64_t mInput;
		uint64_t mOutput;
		FArdaPixelSortShaderConstants mConstants;
	};
}
