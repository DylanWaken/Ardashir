/** Execute an independently compiled graph plan. */
#pragma once

#include "Inductor/Compiler/ArdaInductorCompileResult.h"
#include "Inductor/Runtime/ArdaDependencyFrame.h"
#include "Inductor/Runtime/ArdaDependencyGraphExecution.h"
#include <EASTL/unique_ptr.h>

namespace arda
{
	class FArdaDependencyGraph;

	/** Owns frame execution separately from the graph used for preprocessing and compilation. */
	class FArdaGraphExecutor final
	{
	public:
		/** Claim a successful standalone plan. Copies of that plan cannot create another executor. */
		explicit FArdaGraphExecutor(FArdaInductorCompileResult Result);
		/** Release this plan's execution resources and retained graph declarations. */
		~FArdaGraphExecutor();
		/** Execution ownership cannot be copied. */
		FArdaGraphExecutor(const FArdaGraphExecutor&) = delete;
		/** Execution ownership cannot be reassigned by copy. */
		FArdaGraphExecutor& operator=(const FArdaGraphExecutor&) = delete;
		/** Return plan validation or ownership-claim failure, if any. */
		const FArdaRHIStatus& GetStatus() const noexcept;
		/** Submit one frame without waiting; retain the returned ticket for completion. */
		TArdaRHIResult<FArdaDependencyFrameTicket> Submit(const FArdaGraphExecuteOptions& Options = {});
		/** Wait for this executor's ticket and return completed frame results. */
		FArdaGraphExecutionResult Wait(const FArdaDependencyFrameTicket& Ticket);
		/** Check completion of this executor's ticket without waiting. */
		TArdaRHIResult<bool> IsComplete(const FArdaDependencyFrameTicket& Ticket) const;
		/** Submit and wait for one frame, propagating initialization or execution failures. */
		FArdaGraphExecutionResult Execute(const FArdaGraphExecuteOptions& Options = {});

	private:
		/** Private graph facade retaining this executor's plan and runtime. */
		eastl::unique_ptr<FArdaDependencyGraph> mGraph;
		/** Construction status retained for all subsequent operations. */
		FArdaRHIStatus mStatus;
	};
}
