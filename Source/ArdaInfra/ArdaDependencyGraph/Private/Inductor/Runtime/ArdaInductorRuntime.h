#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"
#include "Inductor/Runtime/ArdaInductorFrame.h"

#include <EASTL/unordered_set.h>

namespace arda
{
	struct FArdaInductorGraphBinding
	{
		FArdaDependencyGraph::FArdaImpl* mGraph = nullptr;
	};
	struct FArdaInductorAdaptiveJob;

	/** Device-bound slots and scheduling state, separate from semantic topology. */
	struct FArdaInductorRuntime
	{
		eastl::vector<eastl::unique_ptr<FArdaInductorFrame>> mFrames;
		eastl::shared_ptr<FArdaDependencyFrameTicket::FArdaState> mPrevious;
		uint32_t mNextSlot = 0;
		bool mbHasSharedResources = false;
		FArdaMemoryPlan mMemoryPlan;
		eastl::shared_ptr<const FArdaMemoryPlan> mAdaptivePlan;
		eastl::shared_ptr<FArdaInductorGraphBinding> mBinding;
		eastl::shared_ptr<const FArdaDependencyGraph::FArdaImpl> mAdaptiveSeed;
		eastl::shared_ptr<const eastl::vector<FArdaInductorReuseFrame>> mReuseFrames;
		~FArdaInductorRuntime();
		static FArdaRHIStatus Prepare(FArdaDependencyGraph::FArdaImpl& Graph,
		    FArdaMemoryPlan Plan,
		    const eastl::vector<FArdaInductorReuseFrame>* Reuse = nullptr);
		static void CollectTelemetry(FArdaDependencyGraph::FArdaImpl& Graph);
		static void CollectStateTelemetry(FArdaDependencyFrameTicket::FArdaState& State);
		static bool TryRetireAll(FArdaDependencyGraph::FArdaImpl& Graph);
		static TArdaRHIResult<FArdaDependencyFrameTicket> Submit(FArdaDependencyGraph::FArdaImpl& Graph,
		    const FArdaGraphExecuteOptions& Options);
		static FArdaGraphExecutionResult Wait(FArdaDependencyGraph::FArdaImpl& Graph,
		    const FArdaDependencyFrameTicket& Ticket);
		static TArdaRHIResult<bool> IsComplete(FArdaDependencyGraph::FArdaImpl& Graph,
		    const FArdaDependencyFrameTicket& Ticket);
		FArdaRHIStatus WaitAll();
		static FArdaGraphExecutionResult Execute(FArdaDependencyGraph::FArdaImpl& Graph,
		    const FArdaGraphExecuteOptions& Options);
		static FArdaGraphExecutionResult WaitState(FArdaDependencyFrameTicket::FArdaState& State);

	private:
		static FArdaRHIStatus MaterializePendingPlan(FArdaDependencyGraph::FArdaImpl& Graph);
		static FArdaRHIStatus MaterializeFrame(FArdaDependencyGraph::FArdaImpl& Graph,
		    FArdaInductorRuntime& Runtime,
		    const FArdaMemoryPlan& Plan,
		    uint32_t FrameIndex,
		    const eastl::vector<FArdaInductorReuseFrame>* Reuse,
		    FArdaRHICommandListRef& ImportStateQuery,
		    eastl::unique_ptr<FArdaInductorFrame>& Frame);
		static FArdaRHIStatus PrepareFramePipelines(FArdaDependencyGraph::FArdaImpl& Graph,
		    FArdaInductorFrame* Frame,
		    const eastl::unordered_set<uint32_t>& SkippedNodes,
		    const eastl::vector<FArdaInductorReuseFrame>* Reuse);
		static FArdaRHIStatus LowerFrame(FArdaDependencyGraph::FArdaImpl& Graph,
		    FArdaInductorRuntime& Runtime,
		    const FArdaMemoryPlan& Plan,
		    FArdaInductorFrame* Frame,
		    const eastl::unordered_set<uint32_t>& SkippedNodes,
		    const eastl::vector<FArdaInductorReuseFrame>* Reuse);
		static FArdaRHIStatus FinalizeFrameBoundary(FArdaInductorFrame& Storage,
		    const eastl::vector<uint32_t>& OrderedAliasedImages,
		    const eastl::unordered_map<uint32_t, FArdaInductorCommandHandle>& ImageRetirements);
		static FArdaInductorRecordFunction MakeNodeRecorder(const eastl::vector<FArdaGraphNodeHandle>& Nodes,
		    bool bCuda,
		    eastl::shared_ptr<FArdaCudaGraphCache> CudaCache,
		    FArdaRHITimerQueryRef Timer,
		    eastl::shared_ptr<FArdaCudaTimingQuery> CudaTimer,
		    eastl::shared_ptr<FArdaInductorGraphBinding> Binding,
		    FArdaInductorFrame* FramePtr);
	};
}
