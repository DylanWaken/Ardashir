/** @file ArdaCudaTiming.h
 * CUDA stream timing regions and completion queries.
 */
#pragma once

#include "RHI/Resources/ArdaRHITypes.h"
#include <EASTL/functional.h>
#include <mutex>

namespace arda
{
	/** GPU elapsed time for an explicitly delimited region of one CUDA stream. */
	struct FArdaCudaTimingRegionSample
	{
		uint64_t mRegionId = 0;
		double mGpuSeconds = 0;
	};

	struct FArdaCudaTimingResult
	{
		bool mbReady = false;
		eastl::vector<FArdaCudaTimingRegionSample> mRegions;
	};

	/** Optional, single-outstanding-sample event query. Poll consumes a completed sample without
	 * waiting for GPU work. The caller must first establish completion of all related graphics/CUDA
	 * submissions (for example with PollSubmission) and pass SubmissionComplete=true. False returns
	 * pending without querying events, avoiding stale recordings during CUDA Graph/CiG replay.
	 * Reuse after consumption or cancellation of an unsubmitted recording.
	 * Use one query per in-flight frame/batch. Unsupported telemetry is reported by Poll rather
	 * than preventing the CUDA work. Measurements include intervening device scheduling delays.
	 */
	class FArdaCudaTimingQuery
	{
	public:
		[[nodiscard]] TArdaRHIResult<FArdaCudaTimingResult> Poll(bool SubmissionComplete = false);

	private:
		friend struct FArdaCudaTimingQueryAccess;
		std::mutex mMutex;
		eastl::shared_ptr<void> mNativeState;
		eastl::function<TArdaRHIResult<FArdaCudaTimingResult>()> mPoll;
	};

	/** Half-open operation range within a sequence; empty regions are omitted. */
	struct FArdaCudaTimingRegion
	{
		uint64_t mRegionId = 0;
		uint32_t mFirstOperation = 0;
		uint32_t mEndOperation = 0;
	};
}
