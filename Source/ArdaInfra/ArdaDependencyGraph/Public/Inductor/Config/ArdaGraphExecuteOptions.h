/** Command recording policy for one graph execution. */
#pragma once

#include <cstdint>

namespace arda
{
	/** Configures command-list recording for graph execution. */
	struct FArdaGraphExecuteOptions
	{
		/** Records independent pass waves concurrently when true. */
		bool mbParallelRecording = true;

		/** Maximum recording workers, or zero to use hardware concurrency. */
		uint32_t mMaxRecordingThreads = 0;

		/** Captures and validates RHI/native state at every graph checkpoint. */
		bool mbValidateResourceStates = true;
	};
}
