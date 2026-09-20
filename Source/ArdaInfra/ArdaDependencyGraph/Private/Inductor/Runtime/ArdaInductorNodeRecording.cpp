#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "Inductor/Compiler/ArdaInductorCommandProgramInternal.h"
#include "Inductor/Runtime/ArdaInductorRuntimeStatus.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <EASTL/unordered_set.h>
#include <cstring>

namespace arda
{
	FArdaInductorRecordFunction FArdaInductorRuntime::MakeNodeRecorder(const eastl::vector<FArdaGraphNodeHandle>& Nodes,
	    bool bCuda,
	    eastl::shared_ptr<FArdaCudaGraphCache> CudaCache,
	    FArdaRHITimerQueryRef Timer,
	    eastl::shared_ptr<FArdaCudaTimingQuery> CudaTimer,
	    eastl::shared_ptr<FArdaInductorGraphBinding> Binding,
	    FArdaInductorFrame* FramePtr)
	{
		return [Nodes, bCuda, CudaCache, Timer, CudaTimer, Binding, FramePtr](FArdaInductorPassContext& PassContext)
		{
			auto* GraphPtr = Binding->mGraph;
			const bool SampleTiming = FramePtr->mActive && FramePtr->mActive->mbSampleTiming;
			const bool RecordTimer =
			    Timer && SampleTiming && bool(PassContext.mUnsafeRawCommandList.BeginTimerQuery(*Timer));
			const auto DropTimer = [&]
			{
				auto& State = *FramePtr->mActive;
				std::lock_guard<std::mutex> Lock(State.mMutex);
				State.mTimers.erase(eastl::remove_if(State.mTimers.begin(),
				                        State.mTimers.end(),
				                        [&](const auto& Pending)
				                        {
					                        return Pending.mQuery == Timer;
				                        }),
				    State.mTimers.end());
			};
			if (Timer && SampleTiming && !RecordTimer)
			{
				// An unrecorded query stays idle and can never become available.
				DropTimer();
			}
			FArdaDependencyExecutionContext Context;
			Context.mGraph = GraphPtr;
			Context.mFrame = FramePtr;
			Context.mPass = &PassContext;
			if (bCuda)
			{
				FArdaCudaSequence Sequence(GraphPtr->mDevice,
				    PassContext.mUnsafeRawCommandList.GetQueueType(),
				    CudaCache);
				for (auto Handle : Nodes)
				{
					Context.mNode = Handle;
					const auto& Node = GraphPtr->mTopology.TryGetNode(Handle)->mPayload;
					if (CudaTimer && SampleTiming)
					{
						PassContext.ReportStatus(Sequence.BeginTimingRegion(CudaTimer, Handle.GetIndex()));
					}
					if (!PassContext.GetStatus())
					{
						return;
					}
					PassContext.ReportStatus(Node.mDefinition->mPrepareCuda(Context, Node.mParameters.get(), Sequence));
					if (CudaTimer && SampleTiming)
					{
						PassContext.ReportStatus(Sequence.EndTimingRegion());
					}
					if (!PassContext.GetStatus())
					{
						return;
					}
				}
				PassContext.ReportStatus(Sequence.DispatchDeferred(PassContext.mUnsafeRawCommandList));
			}
			else
			{
				Context.mNode = Nodes.front();
				const auto& Node = GraphPtr->mTopology.TryGetNode(Context.mNode)->mPayload;
				if (Node.mDefinition->mRecord)
				{
					PassContext.ReportStatus(Node.mDefinition->mRecord(Context, Node.mParameters.get()));
				}
			}
			if (RecordTimer)
			{
				if (auto Status = PassContext.mUnsafeRawCommandList.EndTimerQuery(*Timer); !Status)
				{
					DropTimer();
					// A provider may retain an unfinished native query. Do not submit that recording.
					PassContext.ReportStatus(eastl::move(Status));
				}
			}
		};
	}
}
