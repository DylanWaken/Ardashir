#include <mutex>
#include "RHI/Scheduling/ArdaCudaTiming.h"

namespace arda
{
	TArdaRHIResult<FArdaCudaTimingResult> FArdaCudaTimingQuery::Poll(bool SubmissionComplete)
	{
		if (!SubmissionComplete)
		{
			return {{}, {}};
		}
		std::unique_lock<std::mutex> Lock(mMutex, std::try_to_lock);
		if (!Lock.owns_lock() || !mPoll)
		{
			return {{}, {}};
		}
		auto Result = mPoll();
		if (!Result || Result.mValue.mbReady)
		{
			mPoll = {};
			mNativeState.reset();
		}
		return Result;
	}
}
