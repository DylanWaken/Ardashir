#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "Inductor/Compiler/ArdaInductorCommandProgramInternal.h"
#include "Inductor/Runtime/ArdaInductorRuntimeStatus.h"
#include "Inductor/Runtime/ArdaInductorCudaContract.h"
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
					const size_t FirstOperation = Sequence.GetOperationCount();
					PassContext.ReportStatus(Node.mDefinition->mPrepareCuda(Context, Node.mParameters.get(), Sequence));
					PassContext.ReportStatus(Sequence.GetStatus());
					if (PassContext.GetStatus() && Sequence.GetOperationCount() < FirstOperation)
					{
						PassContext.ReportStatus(MakeArdaInductorRuntimeError(
						    "CUDA nodes may append operations but cannot replace the shared sequence."));
					}
					if (PassContext.GetStatus() && Sequence.GetOperationCount() > FirstOperation)
					{
						eastl::vector<FArdaInductorCudaResourceAccess> Declared;
						for (const auto& Access : Node.mDesc.mAccesses)
						{
							const auto& Resource = GraphPtr->mResources[Access.mResource.mIndex];
							FArdaRHIResourceRef Native = Resource.mbTexture
							    ? FArdaRHIResourceRef(Context.GetTexture(Access.mResource))
							    : FArdaRHIResourceRef(Context.GetBuffer(Access.mResource));
							Declared.push_back({eastl::move(Native), Access});
						}
						if (Node.mDesc.mTransientWorkspaceBytes)
						{
							FArdaDependencyAccess Workspace;
							Workspace.mAccess = EArdaDependencyAccess::ReadWrite;
							Workspace.mState = EArdaRHIResourceState::UnorderedAccess;
							Workspace.mBufferRange = {0, Node.mDesc.mTransientWorkspaceBytes};
							Declared.push_back({Context.GetWorkspaceBuffer(), Workspace});
						}
						for (size_t Operation = FirstOperation;
						    PassContext.GetStatus() && Operation < Sequence.GetOperationCount();
						    ++Operation)
						{
							PassContext.ReportStatus(
							    ValidateArdaInductorCudaBindings(Sequence.GetOperationBindings(Operation), Declared));
						}
					}
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
				if (!Node.mGraphicsStages.empty())
				{
					FArdaGraphicsWork Work;
					for (size_t StageIndex = 0; StageIndex < Node.mGraphicsStages.size(); ++StageIndex)
					{
						const auto StageHandle = Node.mGraphicsStages[StageIndex];
						const auto& Stage = GraphPtr->mTopology.TryGetNode(StageHandle)->mPayload;
						PassContext.ReportStatus(Stage.mDefinition->mConfigureGraphics(Context,
						    Node.mGraphicsStageParameters[StageIndex].get(),
						    Work));
						if (!PassContext.GetStatus())
						{
							return;
						}
					}
					const bool Meshlet = Node.mDesc.mPipelines.front().mKind == EArdaPipelineStateKind::Meshlet;
					PassContext.ReportStatus(Meshlet ? Context.SetMeshletState(eastl::move(Work.mMeshletState))
					                                 : Context.SetGraphicsState(eastl::move(Work.mState)));
					if (!PassContext.GetStatus())
					{
						return;
					}
					if (Meshlet)
					{
						PassContext.ReportStatus(Context.GetCommands().DispatchMesh(Work.mMeshGroupCountX,
						    Work.mMeshGroupCountY,
						    Work.mMeshGroupCountZ));
					}
					else if (Work.mbIndexed)
					{
						Context.GetCommands().DrawIndexed(Work.mDraw);
					}
					else
					{
						Context.GetCommands().Draw(Work.mDraw);
					}
				}
				else if (Node.mDefinition->mRecord)
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
