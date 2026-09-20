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
#include <EASTL/string.h>

namespace arda
{
	FArdaRHIStatus FArdaInductorRuntime::LowerFrame(FArdaDependencyGraph::FArdaImpl& Graph,
	    FArdaInductorRuntime& Runtime,
	    const FArdaMemoryPlan& Plan,
	    FArdaInductorFrame* Frame,
	    const eastl::unordered_set<uint32_t>& SkippedNodes,
	    const eastl::vector<FArdaInductorReuseFrame>* Reuse)
	{
		const auto FrameIndex = Frame->mIndex;
		eastl::unordered_map<uint32_t, FArdaInductorCommandHandle> Passes;
		eastl::unordered_map<uint32_t, FArdaInductorCommandHandle> ImageActivations, ImageRetirements;
		eastl::unordered_set<uint32_t> AliasedImages;
		for (const auto& Activation : Plan.mAliasActivations)
		{
			if (Plan.mRequests[Activation.mResource].mKind == EArdaMemoryKind::Texture)
			{
				AliasedImages.insert(Activation.mResource);
			}
		}
		eastl::vector<uint32_t> OrderedAliasedImages(AliasedImages.begin(), AliasedImages.end());
		eastl::sort(OrderedAliasedImages.begin(), OrderedAliasedImages.end());
		const auto AddImageBoundary = [&](uint32_t Resource, bool Activate) -> FArdaInductorCommandHandle
		{
			FArdaInductorCommandAccesses Accesses;
			Accesses.mTextures.push_back(
			    {Frame->mTextures[Resource]->GetHandle(), {}, EArdaRHIResourceState::Common, false});
			const eastl::string Name =
			    eastl::string(Activate ? "Activate image " : "Retire image ") + eastl::to_string(Resource);
			const auto Pass =
			    Frame->mLowered->AppendCommand(Name, EArdaRHIQueueType::Graphics, eastl::move(Accesses), {}, true);

			if (Activate)
			{
				Frame->mLowered->mImpl->mReusableAliasActivations.push_back(
				    {Pass, EArdaGraphResourceType::Texture, Frame->mTextures[Resource]->GetHandle().GetIndex()});
			}
			return Pass;
		};

		eastl::unordered_map<uint32_t, const FArdaInductorDispatch*> Dispatches;
		for (const auto& Dispatch : Graph.mCompile.mDispatches)
		{
			Dispatches.emplace(Dispatch.mNodes.front().GetIndex(), &Dispatch);
		}
		for (size_t Position = 0; Position < Graph.mCompile.mExecutionOrder.size(); ++Position)
		{
			const auto Head = Graph.mCompile.mExecutionOrder[Position];
			for (const auto& Activation : Plan.mAliasActivations)
			{
				const auto Resource = Activation.mResource;
				if (!AliasedImages.count(Resource) || Plan.mRequests[Resource].mFirstUse != Position)
				{
					continue;
				}
				const auto Pass = AddImageBoundary(Resource, true);
				ImageActivations.emplace(Resource, Pass);
				for (const auto Previous : Activation.mPreviousResources)
				{
					const auto Retired = ImageRetirements.find(Previous);
					if (Retired == ImageRetirements.end())
					{
						return MakeArdaInductorRuntimeError(
						    "An aliased image becomes active before its predecessor retires.");
					}
					Frame->mLowered->AddDependency(Retired->second, Pass);
				}
			}
			if (Passes.count(Head.GetIndex()) || SkippedNodes.count(Head.GetIndex()))
			{
				continue;
			}

			const auto Selected = Dispatches.find(Head.GetIndex());
			if (Selected == Dispatches.end())
			{
				return MakeArdaInductorRuntimeError("Executable node is missing a compiled dispatch.");
			}
			auto Nodes = Selected->second->mNodes;
			const auto& HeadNode = Graph.mTopology.TryGetNode(Head)->mPayload;
			const bool bCuda = HeadNode.mDefinition->mKind == EArdaDependencyNodeKind::Cuda;
			const auto Queue = Graph.mCompile.mQueues[Position];
			FArdaInductorCommandAccesses Accesses;
			for (auto Handle : Nodes)
			{
				const auto* Record = Graph.mTopology.TryGetNode(Handle);
				if (!Record)
				{
					return MakeArdaInductorRuntimeError("A compiled node handle is stale.");
				}
				const auto& Node = Record->mPayload;
				if (bCuda &&
				    (!Node.mDefinition->mPrepareCuda || Node.mDefinition->mKind != EArdaDependencyNodeKind::Cuda))
				{
					return MakeArdaInductorRuntimeError("A CUDA batch contains an incompatible node definition.");
				}
				for (const auto& Access : Node.mDesc.mAccesses)
				{
					if (!Graph.HasResource(Access.mResource))
					{
						return MakeArdaInductorRuntimeError("A compiled resource handle is stale.");
					}
					const auto Index = Access.mResource.mIndex;
					const bool Write = Access.mAccess != EArdaDependencyAccess::Read;
					if (Graph.mResources[Index].mExternalAccelerationStructure)
					{
						if (!Frame->mAccelerationStructures[Index])
						{
							return MakeArdaInductorRuntimeError("A live acceleration structure has no allocation.");
						}
						Accesses.mAccelerationStructures.push_back(
						    {Frame->mAccelerationStructures[Index]->GetHandle(), Access.mState, Write});
					}
					else if (Graph.mResources[Index].mbTexture)
					{
						if (!Frame->mTextures[Index])
						{
							return MakeArdaInductorRuntimeError("A live texture has no allocation.");
						}
						Accesses.mTextures.push_back(
						    {Frame->mTextures[Index]->GetHandle(), Access.mTextureRange, Access.mState, Write});
					}
					else
					{
						if (!Frame->mBuffers[Index])
						{
							return MakeArdaInductorRuntimeError("A live buffer has no allocation.");
						}
						Accesses.mBuffers.push_back(
						    {Frame->mBuffers[Index]->GetHandle(), Access.mBufferRange, Access.mState, Write});
					}
				}
				if (const auto Workspace = Graph.mCompile.mWorkspaceResourceIds.find(Handle.GetIndex());
				    Workspace != Graph.mCompile.mWorkspaceResourceIds.end())
				{
					const auto Index = Workspace->second;
					if (Index >= Frame->mBuffers.size() || !Frame->mBuffers[Index])
					{
						return MakeArdaInductorRuntimeError("A compiled workspace has no allocation.");
					}
					Accesses.mBuffers.push_back(
					    {Frame->mBuffers[Index]->GetHandle(), {}, EArdaRHIResourceState::UnorderedAccess, true});
				}
			}
			eastl::shared_ptr<FArdaCudaGraphCache> CudaCache;
			if (bCuda)
			{
				if (Reuse)
				{
					const auto Found = (*Reuse)[FrameIndex].mCudaCaches.find(Head.GetIndex());
					if (Found != (*Reuse)[FrameIndex].mCudaCaches.end())
					{
						CudaCache = Found->second;
					}
				}
				if (!CudaCache)
				{
					CudaCache = eastl::make_shared<FArdaCudaGraphCache>(Graph.mOptions.mCudaGraphMode);
				}
				Frame->mCudaCaches.emplace(Head.GetIndex(), CudaCache);
			}
			FArdaRHITimerQueryRef Timer;
			eastl::shared_ptr<FArdaCudaTimingQuery> CudaTimer;
			if (Graph.mOptions.mbEnableGpuTiming)
			{
				if (bCuda)
				{
					if (Reuse)
					{
						for (const auto& Old : (*Reuse)[FrameIndex].mCudaTimers)
						{
							if (Old.mNodes == Nodes)
							{
								CudaTimer = Old.mQuery;
							}
						}
					}
					if (!CudaTimer)
					{
						CudaTimer = eastl::make_shared<FArdaCudaTimingQuery>();
					}
					FArdaInductorCudaFrameTimer RegionTimer;
					RegionTimer.mQuery = CudaTimer;
					RegionTimer.mNodes = Nodes;
					RegionTimer.mQueue = Queue;
					for (auto Handle : Nodes)
					{
						RegionTimer.mNames.push_back(Graph.mTopology.TryGetNode(Handle)->mPayload.mName);
					}
					Frame->mCudaTimers.push_back(eastl::move(RegionTimer));
				}
				else if (Graph.mDevice->GetCapabilities().mQueues.SupportsTimestamps(Queue))
				{
					if (Reuse)
					{
						for (const auto& Old : (*Reuse)[FrameIndex].mTimers)
						{
							if (Old.mNodes == Nodes)
							{
								Timer = Old.mQuery;
							}
						}
					}
					if (!Reuse)
					{
						auto Created = Graph.mDevice->CreateTimerQuery();
						if (Created)
						{
							Timer = eastl::move(Created.mValue);
						}
					}
					if (Timer)
					{
						Frame->mTimers.push_back({HeadNode.mName, Timer, Nodes, Queue});
					}
				}
			}
			// AS lifecycle state comes from accepted build/copy submissions, not a generic write declaration.
			// Record after ordered producers submit so native and facade source validation observe that state.
			const bool RecordAtSubmit = bCuda || !Accesses.mAccelerationStructures.empty();
			const auto Pass = Frame->mLowered->AppendCommand(HeadNode.mName,
			    Queue,
			    eastl::move(Accesses),
			    MakeNodeRecorder(Nodes, bCuda, CudaCache, Timer, CudaTimer, Runtime.mBinding, Frame),
			    bCuda,
			    RecordAtSubmit);
			for (auto Handle : Nodes)
			{
				Passes.emplace(Handle.GetIndex(), Pass);
				for (const auto& Access : Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mAccesses)
				{
					const auto Active = ImageActivations.find(Access.mResource.mIndex);
					if (Active != ImageActivations.end())
					{
						Frame->mLowered->AddDependency(Active->second, Pass);
					}
				}
			}
			for (const auto Resource : OrderedAliasedImages)
			{
				if (Plan.mRequests[Resource].mLastUse != Position)
				{
					continue;
				}
				const auto Retire = AddImageBoundary(Resource, false);
				ImageRetirements.emplace(Resource, Retire);
				// Includes every real user, not merely the last node in linear order.
				for (const auto User : Plan.mRequests[Resource].mLastUseNodes)
				{
					const auto Found = Passes.find(User);
					if (Found == Passes.end())
					{
						return MakeArdaInductorRuntimeError("An image retirement precedes an actual user.");
					}
					Frame->mLowered->AddDependency(Found->second, Retire);
				}
			}
		}
		const auto AddDependency = [&](FArdaGraphNodeHandle Producer, FArdaGraphNodeHandle Consumer) -> FArdaRHIStatus
		{
			const auto P = Passes.find(Producer.GetIndex()), C = Passes.find(Consumer.GetIndex());
			if (P == Passes.end() || C == Passes.end() || P->second == C->second)
			{
				return {};
			}
			if (C->second < P->second)
			{
				return MakeArdaInductorRuntimeError("Lowering encountered a dependency against schedule order.");
			}
			Frame->mLowered->AddDependency(P->second, C->second);
			return {};
		};

		for (const auto& Edge : Graph.mCompile.mDispatchDependencies)
		{
			const auto Producer = Graph.mCompile.mDispatches[Edge.first].mNodes.front();
			const auto Consumer = Graph.mCompile.mDispatches[Edge.second].mNodes.front();
			if (auto Status = AddDependency(Producer, Consumer); !Status)
			{
				return Status;
			}
		}
		for (const auto& Activation : Plan.mAliasActivations)
		{
			if (AliasedImages.count(Activation.mResource))
			{
				continue;
			}
			const auto Consumer = Passes.find(Activation.mConsumer);
			if (Consumer == Passes.end())
			{
				continue;
			}
			const auto Index = Activation.mResource;
			if (Index >= Frame->mBuffers.size() || !Frame->mBuffers[Index])
			{
				return MakeArdaInductorRuntimeError("A placed buffer activation has no physical resource.");
			}
			Frame->mLowered->mImpl->mReusableAliasActivations.push_back(
			    {Consumer->second, EArdaGraphResourceType::Buffer, Frame->mBuffers[Index]->GetHandle().GetIndex()});
		}
		return FinalizeFrameBoundary(*Frame, OrderedAliasedImages, ImageRetirements);
	}
}
