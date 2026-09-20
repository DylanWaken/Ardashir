#pragma once

#include "Inductor/Runtime/ArdaDependencyFrame.h"
#include "Inductor/Runtime/ArdaInductorTimingState.h"
#include "Inductor/Runtime/ArdaInductorReadbackCompletions.h"

namespace arda
{
	/** Completion ownership is independent of a reusable frame slot and survives edits. */
	struct FArdaDependencyFrameTicket::FArdaState
	{
		FArdaRHIDeviceRef mDevice;
		uint64_t mGraphIdentity = 0;
		uint64_t mSequence = 0;
		FArdaGraphExecutionResult mExecution;
		FArdaRHIStatus mRetirementStatus;
		eastl::vector<FArdaRHIGpuFenceRef> mRecoveryFences;
		eastl::vector<FArdaInductorFrameTimer> mTimers;
		eastl::vector<FArdaInductorCudaFrameTimer> mCudaTimers;
		bool mbSampleTiming = false;
		uint64_t mTimingEpoch = 0;
		eastl::shared_ptr<FArdaInductorTimingAccumulator> mTiming;
		FArdaInductorReadbackCompletions mReadbackCompletions;
		std::mutex mMutex;
		bool mbComplete = false;
	};
}
