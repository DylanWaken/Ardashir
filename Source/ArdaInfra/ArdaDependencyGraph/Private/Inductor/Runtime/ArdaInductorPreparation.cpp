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
	FArdaRHIStatus FArdaInductorRuntime::MaterializePendingPlan(FArdaDependencyGraph::FArdaImpl& Graph)
	{
		if (Graph.mRuntime || !Graph.mDevice)
		{
			return MakeArdaInductorRuntimeError("A pending plan requires a device and no previous physical pool.");
		}

		struct FPreparationGuard
		{
			bool& mExecuting;

			explicit FPreparationGuard(bool& Executing)
			    : mExecuting(Executing)
			{
				mExecuting = true;
			}

			~FPreparationGuard()
			{
				mExecuting = false;
			}
		} Guard(Graph.mbExecuting);

		try
		{
			// Stage host storage before publishing the physical runtime. A failed preparation
			// retains the pending logical plan and cannot expose partially committed pools.
			auto PersistentBuffers = Graph.mPersistentBuffers;
			auto PersistentTextures = Graph.mPersistentTextures;
			PersistentBuffers.resize(Graph.mResources.size());
			PersistentTextures.resize(Graph.mResources.size());
			// Compilation released the retired pool. Flush deferred native destruction before
			// allocating its replacement, preserving the graph's peak-allocation boundary.
			Graph.mDevice->RunGarbageCollection();
			if (auto Status = Prepare(Graph, *Graph.mPendingMemoryPlan); !Status)
			{
				return Status;
			}
			for (size_t Index = 0; Index < Graph.mResources.size(); ++Index)
			{
				if (Graph.mResources[Index].mbPersistent)
				{
					PersistentBuffers[Index] = Graph.mRuntime->mFrames.front()->mMemory.mBuffers[Index];
					PersistentTextures[Index] = Graph.mRuntime->mFrames.front()->mMemory.mTextures[Index];
				}
			}
			Graph.mPersistentBuffers = eastl::move(PersistentBuffers);
			Graph.mPersistentTextures = eastl::move(PersistentTextures);
			Graph.mPendingMemoryPlan.reset();
			return {};
		}
		catch (const std::exception& Error)
		{
			Graph.mRuntime.reset();
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
		}
		catch (...)
		{
			Graph.mRuntime.reset();
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			    "Graph resource preparation threw an exception.");
		}
	}

	FArdaRHIStatus FArdaInductorRuntime::Prepare(FArdaDependencyGraph::FArdaImpl& Graph,
	    FArdaMemoryPlan Plan,
	    const eastl::vector<FArdaInductorReuseFrame>* Reuse)
	{
		if (!Graph.mDevice)
		{
			return MakeArdaInductorRuntimeError("Device-bound graph preparation requires a device.");
		}
		if (Graph.mRuntime)
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
			if (Node.mDesc.mbPipelineStageOnly)
			{
				if (!Node.mDesc.mAccesses.empty() || !Node.mDesc.mColorTargets.empty() || Node.mDesc.mDepthTarget)
				{
					return MakeArdaInductorRuntimeError("A pipeline-stage-only node cannot access physical resources.");
				}
				SkippedNodes.insert(Handle.GetIndex());
			}
			else if (Node.mDefinition->mKind == EArdaDependencyNodeKind::Synchronization &&
			    !Node.mDefinition->mRecord && Node.mDesc.mAccesses.empty())
			{
				// Pure synchronization is represented by contracted dependency edges.
				SkippedNodes.insert(Handle.GetIndex());
			}
		}
		auto Runtime = eastl::make_unique<FArdaInductorRuntime>();
		Runtime->mMemoryPlan = Plan;
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
		Graph.mRuntime = eastl::move(Runtime);
		return {};
	}
}
