/** Native CUDA capture and deferred batch submission contracts. */
#pragma once

#include "RHI/Providers/ArdaRHIProviderTypes.h"
#include "RHI/CUDA/ArdaRHICuda.h"
#include "RHI/Scheduling/ArdaCudaSemaphore.h"

namespace arda
{
	/** A single-use native capture or deferred ordinary-context batch, retained through fence retirement. */
	class IArdaCudaBatch : public IArdaProviderObject
	{
	public:
		/** Captures native launches, or retains validated ordinary launches without executing them. */
		virtual FArdaRHIStatus Record(void* CommandList,
		    const eastl::vector<FArdaCudaKernel>& Kernels,
		    const eastl::vector<uint64_t>& Bindings) = 0;

		/** Rejects a failed capture, replay, or violation of context capture/submission order. */
		virtual FArdaRHIStatus ValidateSubmit() const = 0;

		/** Advances the context's capture-order gate after native submission succeeds. */
		virtual void MarkSubmitted() = 0;

		/** Configures a GPU wait and signal before submission. Zero values select binary semaphore semantics. */
		virtual void SetSynchronization(eastl::shared_ptr<IArdaCudaSemaphore> Wait,
		    uint64_t WaitValue,
		    eastl::shared_ptr<IArdaCudaSemaphore> Signal,
		    uint64_t SignalValue) = 0;

		/** Enqueues a deferred batch with configured GPU synchronization; unconfigured batches drain on the CPU. */
		virtual FArdaRHIStatus Execute() = 0;
	};
}
