/** Published dependency schedule and physical allocation diagnostics. */
#pragma once

#include "ArdaGraph.h"
#include "Graph/Node/ArdaDependencyNodeDesc.h"
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
	struct FArdaInductorExecutionState;
	struct FArdaInductorCompiledGraph;
	class FArdaDependencyNodeExecutable;

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

	/** Device preparation retained separately from the authored topology and its public parameter schema. */
	struct FArdaInductorNodeState
	{
		/** Described execution effects, prepared shaders, bindings and native pipeline contributions. */
		FArdaDependencyNodeDesc mDesc;
		/** Private bound schema identified by the node definition's mPreparedParameterType. */
		eastl::shared_ptr<const void> mParameters;
		/** Per-draw mutable stage instances; immutable device preparation may be shared. */
		eastl::vector<eastl::shared_ptr<const void>> mGraphicsStageParameters;
	};

	/** Admission contract of a subgraph whose body replaced its interface node during compilation. */
	struct FArdaInductorSubgraphAdmission
	{
		/** Qualified operation name for diagnostics. */
		eastl::string mName;
		/** Named implementation retained for reevaluating its current admission contract. */
		eastl::shared_ptr<const FArdaDependencyNodeExecutable> mDefinition;
		/** Frozen public inputs used when the body was expanded. */
		eastl::shared_ptr<const void> mParameters;
		/** Frozen requirements inferred from the subgraph's declared interface. */
		FArdaDependencyNodeRequirements mRequirements;
		/** Portable parameter payload and named context references retained for later rebinding. */
		eastl::string mArchivedParameters;
		eastl::vector<eastl::string> mExternalParameterNames;
		bool mbUnboundExternalParameters = false;
	};

	/** Immutable schedule products, replaced by successful edit compilation or adaptive schedule adoption. */
	struct FArdaInductorCompileResult
	{
		/** Retained immutable graph input; lets an executor outlive the graph used for compilation. */
		eastl::shared_ptr<const FArdaInductorCompiledGraph> mGraph;
		/** Prepared node state follows complete node identities, including concurrent edit remapping. */
		eastl::unordered_map<FArdaGraphNodeHandle, FArdaInductorNodeState, FArdaGraphNodeHandleHash> mNodes;
		/** Removed subgraph interfaces still participate in admission when a compiled revision is reused. */
		eastl::vector<FArdaInductorSubgraphAdmission> mSubgraphAdmissions;
		/** Opaque device state, prepared allocations, runtime and telemetry; absent in schedule-only proposals. */
		eastl::shared_ptr<FArdaInductorExecutionState> mExecution;
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
		/** Ordered executable partition. Compatible native pipelines share one recording;
		 * CUDA runs share one sequence. Resource hazards and alias boundaries split native batches.
		 */
		eastl::vector<eastl::vector<FArdaGraphNodeHandle>> mPipelineBatches;
		/** Consecutive traditional draws with compatible logical bindings and explicit instancing permission.
		 * Every group has at least two nodes inside one pipeline batch. Recording additionally checks
		 * native geometry/state and contiguous instance ranges before merging; failures retain original draws.
		 */
		eastl::vector<eastl::vector<FArdaGraphNodeHandle>> mInstancedDraws;
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
