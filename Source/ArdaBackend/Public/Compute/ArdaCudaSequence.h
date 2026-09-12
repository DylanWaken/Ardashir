/** @file ArdaCudaSequence.h
 * Ordered compositions of CUDA kernels and external calls with one graphics/CUDA handoff.
 */
#pragma once

#include "ArdaComputeOperand.h"

namespace arda
{
	/** Completion identity for a sequence. A zero instance denotes an empty/all-NoWork sequence. */
	struct FArdaCudaSequenceSubmission
	{
		/** Retains the device owning the completion identity. */
		FArdaRHIDeviceRef mDevice;
		/** Queue on which the sequence was submitted. */
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
		/** Queue completion identity returned by ExecuteCommandList. */
		uint64_t mInstance = 0;
		/** Number of explicit compiled kernels; excludes opaque library-internal launches and NoWork. */
		size_t mKernelCount = 0;
		/** Number of operations (compiled kernels plus external calls), excluding NoWork. */
		size_t mOperationCount = 0;
	};

	/** Builds a sequence from independently selected single-operation operands.
     * Add freezes each step's values and retains its resources, including scratch buffers.
     * Different operand schemas and buffer views may be mixed. Steps run in insertion order
     * on one provider stream, with graphics synchronization around the entire sequence.
     * The first build error is retained: a failed Add cannot silently submit a partial algorithm.
     * Dispatch may be repeated using fresh command lists. Do not mutate while recording/submitting.
     */
	class FArdaCudaSequence
	{
	public:
		/** Selects a common device/queue before adding any steps. Copy queues are unsupported. */
		explicit FArdaCudaSequence(FArdaRHIDeviceRef Device,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics,
		    eastl::shared_ptr<FArdaCudaGraphCache> GraphCache = {});

		/** Retained native graph cache; an omitted constructor cache creates one in Prefer mode. */
		[[nodiscard]] const eastl::shared_ptr<FArdaCudaGraphCache>& GetGraphCache() const noexcept;

		/** Prepares and appends one operand; neither the operand nor Parameters must outlive this call. */
		template <class OperandType>
		[[nodiscard]] FArdaRHIStatus Add(const OperandType& Operand,
		    const typename OperandType::FParameters& Parameters)
		{
			if (!mStatus)
			{
				return mStatus;
			}
			auto Plan = Operand.PrepareDispatch(Parameters, mQueue);
			return Plan ? AddPlan(Plan.mValue) : (mStatus = Plan.mStatus);
		}

		/** Copies an existing frozen plan, checking its device, queue and single-operation contract. */
		[[nodiscard]] FArdaRHIStatus AddPlan(const eastl::shared_ptr<const FArdaCudaDispatchPlan>& Plan);

		/** Delimits GPU timing around subsequent Add/AddPlan operations without splitting the batch.
		 * Regions cannot nest or repeat IDs; every region in the sequence uses the same query.
		 * Omit regions entirely on unsampled executions for zero event/query operations.
		 */
		[[nodiscard]] FArdaRHIStatus BeginTimingRegion(eastl::shared_ptr<FArdaCudaTimingQuery> Query,
		    uint64_t RegionId);
		[[nodiscard]] FArdaRHIStatus EndTimingRegion();

		/** Returns the first construction error, if any. No GPU work is performed. */
		[[nodiscard]] const FArdaRHIStatus& GetStatus() const noexcept;

		/** Returns the explicit compiled-kernel count; external calls and NoWork are omitted. */
		[[nodiscard]] size_t GetKernelCount() const noexcept;

		/** Returns the compiled-kernel plus external-call count; NoWork steps are omitted. */
		[[nodiscard]] size_t GetOperationCount() const noexcept;

		/** Records one native batch into an open caller-owned list without submitting it. */
		[[nodiscard]] FArdaRHIStatus DispatchDeferred(IArdaRHICommandList& Commands) const;

		/** Creates and submits one command list; completion covers all kernels and retained scratch. */
		[[nodiscard]] TArdaRHIResult<FArdaCudaSequenceSubmission> Dispatch() const;

	private:
		/** Shared device and queue for all steps. */
		FArdaRHIDeviceRef mDevice;
		/** Queue validated at construction. */
		EArdaRHIQueueType mQueue;
		/** Owned single-operation descriptors with immutable snapshots of caller parameters. */
		eastl::vector<FArdaCudaDispatch> mDispatches;
		/** Sticky construction/preparation failure. */
		FArdaRHIStatus mStatus;
		/** Reused by repeated dispatches or explicitly shared across newly prepared sequences. */
		eastl::shared_ptr<FArdaCudaGraphCache> mGraphCache;
		eastl::shared_ptr<FArdaCudaTimingQuery> mTimingQuery;
		eastl::vector<FArdaCudaTimingRegion> mTimingRegions;
		bool mbTimingRegionOpen = false;
	};
}
