/** Collected execution durations and adaptive scheduling diagnostics. */
#pragma once

#include "ArdaGraph.h"
#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Scheduling/ArdaRHIQueueTypes.h"
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace arda
{
	/** Measured node statistics. Every collected execution contributes once, including late frame receipts. */
	struct FArdaInductorTimingSample
	{
		eastl::string mNodeName;
		/** Stable identities; display names are never used as batch or node keys. */
		eastl::vector<FArdaGraphNodeHandle> mNodes;
		/** EMA in collection order, which can differ from frame submission order. */
		double mGpuSeconds = 0;
		uint32_t mSampleCount = 0;
		/** Duration from the highest collected frame sequence; late receipts do not replace it. */
		double mLastGpuSeconds = 0;
		double mMinGpuSeconds = 0;
		double mMaxGpuSeconds = 0;
		/** Highest collected frame sequence, rather than the most recently collected receipt. */
		uint64_t mLastFrameSequence = 0;
		/** Queue used by the frame described by mLastFrameSequence and mLastGpuSeconds. */
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
	};

	/** One completed sampled node execution; durations exclude CPU recording and submission. */
	struct FArdaInductorNodeTiming
	{
		FArdaGraphNodeHandle mNode;
		eastl::string mNodeName;
		uint64_t mFrameSequence = 0;
		double mGpuSeconds = 0;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
	};

	/** Automatic tuning is a bounded heuristic search, not a guarantee of a global optimum. */
	struct FArdaInductorAdaptiveSchedulingStats
	{
		uint64_t mIterations = 0;
		uint64_t mAcceptedSchedules = 0;
		uint64_t mCandidatesExamined = 0;
		bool mbPending = false;
		double mLastBaselineCost = 0;
		double mLastCandidateCost = 0;
		FArdaRHIStatus mLastStatus;
	};
}
