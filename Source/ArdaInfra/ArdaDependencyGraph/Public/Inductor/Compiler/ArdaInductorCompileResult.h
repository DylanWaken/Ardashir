/** Published dependency schedule and physical allocation diagnostics. */
#pragma once

#include "ArdaGraph.h"
#include "RHI/Memory/ArdaMemoryPlanner.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/array.h>
#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Scheduling/ArdaRHIQueueTypes.h"
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <EASTL/utility.h>

namespace arda
{

	/** One native dispatch or CUDA batch. Stream UINT32_MAX denotes native work. */
	struct FArdaInductorDispatch
	{
		eastl::vector<FArdaGraphNodeHandle> mNodes;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
		uint32_t mCudaStream = UINT32_MAX;
	};

	/** Logical fence edge. Runtime resolves dispatch IDs into native submissions/timeline values. */
	struct FArdaInductorFence
	{
		uint32_t mProducer = 0, mConsumer = 0;
	};

	/** Immutable schedule products, replaced by successful edit compilation or adaptive schedule adoption. */
	struct FArdaInductorCompileResult
	{
		/** Allocation proof for mAllocationOrder; rebuild after reordering nodes. */
		eastl::shared_ptr<const FArdaMemoryPlan> mMemoryPlan;
		eastl::vector<FArdaGraphNodeHandle> mAllocationOrder;
		eastl::vector<FArdaInductorDispatch> mDispatches;
		eastl::array<eastl::vector<uint32_t>, ArdaRHIQueueTypeCount> mQueueDispatches;
		/** Ordered dispatch IDs on each CUDA stream. The current runtime uses one framework stream. */
		eastl::vector<eastl::vector<uint32_t>> mCudaStreams;
		eastl::vector<eastl::pair<uint32_t, uint32_t>> mDispatchDependencies;
		eastl::vector<FArdaInductorFence> mFences;
		FArdaRHIStatus mStatus;
		uint64_t mRevision = 0;
		eastl::vector<FArdaGraphNodeHandle> mExecutionOrder;
		eastl::vector<FArdaGraphNodeHandle> mCulledNodes;
		/** One native queue slot per mExecutionOrder entry, at the same index, including declaration
		 * and CUDA nodes. CUDA work executes on its stream; this slot does not move it to a native queue.
		 */
		eastl::vector<EArdaRHIQueueType> mQueues;
		/** Exactly the contiguous runs of executable CUDA nodes in mExecutionOrder, preserving order.
		 * Pure synchronization and stage-only declarations are omitted and do not split runs;
		 * executable non-CUDA nodes split them. Every scheduled CUDA operation appears once.
		 */
		eastl::vector<eastl::vector<FArdaGraphNodeHandle>> mCudaBatches;
		eastl::vector<eastl::pair<FArdaGraphNodeHandle, FArdaGraphNodeHandle>> mMemoryDependencies;
		/** Total bytes accounted for by the selected memory plan, not proof of native allocation. */
		uint64_t mAllocatedBytes = 0;
		uint64_t mPeakLiveBytes = 0;
		uint64_t mAliasedBytes = 0;
		uint32_t mSchedulesExamined = 0;
		uint64_t mSearchStatesExamined = 0;
		/** True only if every legal topological order was evaluated by the current allocator/cost model. */
		bool mbSearchComplete = false;
		bool mbSearchExhausted = false;
		double mEstimatedExecutionCost = 0;
		eastl::unordered_map<uint32_t, uint32_t> mWorkspaceResourceIds;
	};
}
