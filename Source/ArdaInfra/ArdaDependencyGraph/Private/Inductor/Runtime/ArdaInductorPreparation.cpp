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
#include <exception>

namespace arda
{
	namespace
	{
		FArdaRHIStatus InitializeNewPersistentBuffers(FArdaDependencyGraph::FArdaImpl& Graph,
		    const FArdaMemoryPlan& Plan,
		    const FArdaInductorRuntime& Runtime)
		{
			FArdaRHICommandListRef Commands;
			auto& Device = *Graph.mCompile.mExecution->mDevice;
			for (size_t Index = 0; Index < Graph.mResources.size(); ++Index)
			{
				const auto& Initializer = Graph.mResources[Index].mBufferInitializer;
				const auto& Buffer = Runtime.mFrames.front()->mMemory.mBuffers[Index];
				if (Initializer.IsEmpty() || !Buffer || Plan.mRequests[Index].mExternalBuffer)
				{
					continue;
				}
				if (!Commands)
				{
					auto Created = Device.CreateCommandList(EArdaRHIQueueType::Graphics);
					if (!Created)
					{
						return Created.mStatus;
					}
					Commands = eastl::move(Created.mValue);
					if (auto Status = Commands->Open(); !Status)
					{
						return Status;
					}
				}
				if (auto Status = Commands->WriteBuffer(*Buffer, Initializer.GetData(), Initializer.GetSize()); !Status)
				{
					return Status;
				}
				if (auto Status = Commands->SetBufferState(*Buffer, EArdaRHIResourceState::Common); !Status)
				{
					return Status;
				}
			}
			if (!Commands)
			{
				return {};
			}
			if (auto Status = Commands->Close(); !Status)
			{
				return Status;
			}
			const auto Submitted = Device.ExecuteCommandList(Commands);
			if (!Submitted)
			{
				return Submitted.mStatus;
			}
			// Initialization is a cold allocation operation. Publish no pool until its contents
			// are complete; failed preparation discards these allocations and can safely retry.
			return Device.WaitForSubmission(Submitted.mValue);
		}
	}

	FArdaRHIStatus FArdaInductorRuntime::MaterializePlan(FArdaDependencyGraph::FArdaImpl& Graph)
	{
		if (Graph.mCompile.mExecution->mRuntime || !Graph.mCompile.mExecution->mDevice || !Graph.mCompile.mMemoryPlan)
		{
			return MakeArdaInductorRuntimeError(
			    "Materialization requires a compiled plan, a device and no previous physical pool.");
		}

		struct FArdaPreparationGuard
		{
			bool& mExecuting;

			explicit FArdaPreparationGuard(bool& Executing)
			    : mExecuting(Executing)
			{
				mExecuting = true;
			}

			~FArdaPreparationGuard()
			{
				mExecuting = false;
			}
		} Guard(Graph.mbExecuting);

		try
		{
			// Stage host storage before publishing the physical runtime. A failed preparation
			// retains the compiled plan and cannot expose partially committed pools.
			auto PersistentBuffers = Graph.mCompile.mExecution->mPersistentBuffers;
			auto PersistentTextures = Graph.mCompile.mExecution->mPersistentTextures;
			PersistentBuffers.resize(Graph.mResources.size());
			PersistentTextures.resize(Graph.mResources.size());
			// Compilation released the retired pool. Flush deferred native destruction before
			// allocating its replacement, preserving the graph's peak-allocation boundary.
			Graph.mCompile.mExecution->mDevice->RunGarbageCollection();
			if (auto Status = Prepare(Graph, *Graph.mCompile.mMemoryPlan); !Status)
			{
				return Status;
			}
			for (size_t Index = 0; Index < Graph.mResources.size(); ++Index)
			{
				if (Graph.mResources[Index].mbPersistent)
				{
					PersistentBuffers[Index] =
					    Graph.mCompile.mExecution->mRuntime->mFrames.front()->mMemory.mBuffers[Index];
					PersistentTextures[Index] =
					    Graph.mCompile.mExecution->mRuntime->mFrames.front()->mMemory.mTextures[Index];
				}
			}
			Graph.mCompile.mExecution->mPersistentBuffers = eastl::move(PersistentBuffers);
			Graph.mCompile.mExecution->mPersistentTextures = eastl::move(PersistentTextures);
			return {};
		}
		catch (const std::exception& Error)
		{
			Graph.mCompile.mExecution->mRuntime.reset();
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
		}
		catch (...)
		{
			Graph.mCompile.mExecution->mRuntime.reset();
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			    "Graph resource preparation threw an exception.");
		}
	}

	FArdaRHIStatus FArdaInductorRuntime::Prepare(FArdaDependencyGraph::FArdaImpl& Graph,
	    const FArdaMemoryPlan& Plan,
	    const eastl::vector<FArdaInductorReuseFrame>* Reuse)
	{
		if (!Graph.mCompile.mExecution->mDevice)
		{
			return MakeArdaInductorRuntimeError("Device-bound graph preparation requires a device.");
		}
		if (Graph.mCompile.mExecution->mRuntime)
		{
			return MakeArdaInductorRuntimeError(
			    "Release the previous graph pool before materializing its replacement.");
		}
		if (Plan.mRequests.size() < Graph.mResources.size() || !Graph.mOptions.mFramesInFlight)
		{
			return MakeArdaInductorRuntimeError("The memory plan does not cover the graph resource identifier domain.");
		}
		if (Graph.mCompile.mQueues.size() != Graph.mCompile.mExecutionOrder.size())
		{
			return MakeArdaInductorRuntimeError("Compiled graph queue assignment does not cover its schedule.");
		}
		eastl::unordered_set<uint32_t> SkippedNodes;
		for (auto Handle : Graph.mCompile.mExecutionOrder)
		{
			const auto* Record = Graph.mTopology.TryGetNode(Handle);
			if (!Record)
			{
				return MakeArdaInductorRuntimeError("A compiled node handle is stale.");
			}
			const auto& Node = Record->mPayload;
			if (Graph.GetNodeDescription(Node).mbPipelineStageOnly)
			{
				if (!Graph.GetNodeDescription(Node).mAccesses.empty() ||
				    !Graph.GetNodeDescription(Node).mColorTargets.empty() ||
				    Graph.GetNodeDescription(Node).mDepthTarget)
				{
					return MakeArdaInductorRuntimeError("A pipeline-stage-only node cannot access physical resources.");
				}
				SkippedNodes.insert(Handle.GetIndex());
			}
			else if (Node.mDefinition->mKind == EArdaDependencyNodeKind::Synchronization &&
			    !Node.mDefinition->mRecord && Graph.GetNodeDescription(Node).mAccesses.empty())
			{
				// Pure synchronization is represented by contracted dependency edges.
				SkippedNodes.insert(Handle.GetIndex());
			}
		}
		auto Runtime = eastl::make_unique<FArdaInductorRuntime>();
		Runtime->mBinding = eastl::make_shared<FArdaInductorGraphBinding>();
		Runtime->mBinding->mGraph = &Graph;
		if (Reuse && Reuse->size() != Graph.mOptions.mFramesInFlight)
		{
			return MakeArdaInductorRuntimeError("A tuning proposal must reuse every existing frame pool.");
		}
		FArdaRHICommandListRef ImportStateQuery;
		Runtime->mbHasSharedResources = eastl::any_of(Plan.mRequests.begin(),
		    Plan.mRequests.end(),
		    [](const auto& Request)
		    {
			    return !Request.mFirstUseNodes.empty() &&
			        (Request.mbPersistent || Request.mExternalBuffer || Request.mExternalTexture ||
			            Request.mExternalAccelerationStructure);
		    });
		for (uint32_t FrameIndex = 0; FrameIndex < Graph.mOptions.mFramesInFlight; ++FrameIndex)
		{
			eastl::unique_ptr<FArdaInductorFrame> Frame;
			if (auto Status = MaterializeFrame(Graph, *Runtime, Plan, FrameIndex, Reuse, ImportStateQuery, Frame);
			    !Status)
			{
				return Status;
			}
			if (auto Status = PrepareFramePipelines(Graph, Frame.get(), SkippedNodes, Reuse); !Status)
			{
				return Status;
			}
			if (auto Status = LowerFrame(Graph, *Runtime, Plan, Frame.get(), SkippedNodes, Reuse); !Status)
			{
				return Status;
			}
			Runtime->mFrames.push_back(eastl::move(Frame));
		}
		if (Graph.mOptions.mbEnableGpuTiming)
		{
			Runtime->mAdaptivePlan = eastl::make_shared<const FArdaMemoryPlan>(Plan);
			auto Reusable = eastl::make_shared<eastl::vector<FArdaInductorReuseFrame>>();
			for (const auto& Frame : Runtime->mFrames)
			{
				FArdaInductorReuseFrame R;
				R.mMemory = Frame->mMemory;
				R.mNodeBindings = Frame->mNodeBindings;

				R.mTimers = Frame->mTimers;
				R.mCudaTimers = Frame->mCudaTimers;
				R.mCudaCaches = Frame->mCudaCaches;
				R.mBufferStates.resize(Frame->mBuffers.size(), EArdaRHIResourceState::Common);
				R.mTextureStates.resize(Frame->mTextures.size(), EArdaRHIResourceState::Common);
				for (size_t I = 0; I < Frame->mBuffers.size(); ++I)
				{
					if (Frame->mBuffers[I])
					{
						R.mBufferStates[I] = Frame->mBuffers[I]->GetInitialState();
					}
				}
				for (size_t I = 0; I < Frame->mTextures.size(); ++I)
				{
					if (Frame->mTextures[I])
					{
						R.mTextureStates[I] = Frame->mTextures[I]->GetInitialState();
					}
				}
				Reusable->push_back(eastl::move(R));
			}
			Runtime->mReuseFrames = eastl::move(Reusable);
			Runtime->mAdaptiveSeed = CloneArdaInductorTimingInputs(Graph);
		}
		// Adaptive schedule preparation reuses already initialized physical allocations.
		if (!Reuse)
		{
			if (auto Status = InitializeNewPersistentBuffers(Graph, Plan, *Runtime); !Status)
			{
				return Status;
			}
		}
		Graph.mCompile.mExecution->mRuntime = eastl::move(Runtime);
		return {};
	}
}
