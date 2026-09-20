#pragma once

#include "Inductor/Runtime/ArdaDependencyGraphExecution.h"
#include "RHI/Scheduling/ArdaRHICommandList.h"

#include <EASTL/shared_ptr.h>
#include <future>
#include <mutex>

namespace arda
{
	/** The native callback is the sole promise owner after registration. Discarding
	 * an unsubmitted list breaks that promise and cancels its wait automatically.
	 * Completion threads own bytes only; publishing caller destinations is deferred.
	 */
	class FArdaInductorReadbackCompletions
	{
	public:
		[[nodiscard]] FArdaRHIDeviceToHostCopyCallback Register(eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
		    uint32_t PassIndex);
		/** Waits every registered callback, including submitted callbacks after a partial
		 * failure, then publishes all outputs or clears them. Called after recording
		 * storage has released every command list that did not reach submission.
		 */
		[[nodiscard]] FArdaRHIStatus Drain(FArdaRHIStatus SubmissionStatus);
		[[nodiscard]] bool IsReady();

	private:
		struct FArdaPending
		{
			std::future<FArdaRHIBufferReadbackResult> mFuture;
			eastl::shared_ptr<eastl::vector<uint8_t>> mDestination;
			uint32_t mPassIndex = 0;
		};

		std::mutex mMutex;
		eastl::vector<FArdaPending> mPending;
	};
}
