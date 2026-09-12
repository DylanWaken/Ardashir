#include "ArdaInductorPch.h"
#include "ArdaDependencyGraphInternal.h"
#include "ArdaInductorCommandProgramInternal.h"
#include "ArdaInductorCommandExecutor.h"
#include "ArdaInductorState.h"

#include <EASTL/algorithm.h>
#include <EASTL/unordered_set.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace arda
{
	namespace
	{
		FArdaRHIStatus RuntimeError(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, Message);
		}

		EArdaRHIResourceState EntryState(EArdaRHIResourceState State)
		{
			return State == EArdaRHIResourceState::Unknown ? EArdaRHIResourceState::Common : State;
		}

		bool DeclaresResource(const FArdaDependencyGraph::FImpl& Graph,
		    FArdaGraphNodeHandle Node,
		    FArdaDependencyResourceHandle Resource)
		{
			const auto* Record = Graph.mTopology.TryGetNode(Node);
			if (!Record || !Graph.HasResource(Resource))
			{
				return false;
			}
			const auto& Desc = Record->mPayload.mDesc;
			return eastl::any_of(Desc.mAccesses.begin(),
			           Desc.mAccesses.end(),
			           [Resource](const auto& Access)
			           {
				           return Access.mResource == Resource;
			           }) ||
			    eastl::find(Desc.mColorTargets.begin(), Desc.mColorTargets.end(), Resource) !=
			    Desc.mColorTargets.end() ||
			    Desc.mDepthTarget == Resource;
		}
	}

	IArdaRHICommandList& FArdaDependencyExecutionContext::GetCommands() const
	{
		if (!mPass)
		{
			ARDA_CHECK_MSG("Dependency execution context is outside a node callback.");
		}
		return mPass->mUnsafeRawCommandList;
	}

	FArdaRHIDeviceToHostCopyCallback FArdaInductorReadbackCompletions::Register(
	    eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
	    uint32_t PassIndex)
	{
		auto Promise = std::make_shared<std::promise<FArdaRHIBufferReadbackResult>>();
		FPending Pending{Promise->get_future(), eastl::move(Destination), PassIndex};
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			mPending.push_back(eastl::move(Pending));
		}
		return [Promise = std::move(Promise)](FArdaRHIBufferReadbackResult Result)
		{
			Promise->set_value(eastl::move(Result));
		};
	}

	FArdaRHIStatus FArdaInductorReadbackCompletions::Drain(FArdaRHIStatus SubmissionStatus)
	{
		eastl::vector<FPending> Pending;
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			Pending.swap(mPending);
		}
		// Parallel recording order is not semantic order. Multiple nodes targeting
		// one host vector publish in the compiled pass order on this calling thread.
		std::stable_sort(Pending.begin(),
		    Pending.end(),
		    [](const auto& A, const auto& B)
		    {
			    return A.mPassIndex < B.mPassIndex;
		    });
		eastl::vector<FArdaRHIBufferReadbackResult> Results;
		Results.reserve(Pending.size());
		for (auto& Completion : Pending)
		{
			FArdaRHIBufferReadbackResult Result;
			try
			{
				Result = Completion.mFuture.get();
			}
			catch (const std::future_error&)
			{
				Result.mStatus = RuntimeError("Readback completion was discarded before its command list completed.");
			}
			if (SubmissionStatus && !Result)
			{
				SubmissionStatus = Result.mStatus;
			}
			Results.push_back(eastl::move(Result));
		}
		for (size_t Index = 0; Index < Pending.size(); ++Index)
		{
			if (!Pending[Index].mDestination)
			{
				continue;
			}
			if (SubmissionStatus)
			{
				*Pending[Index].mDestination = eastl::move(Results[Index].mValue);
			}
			else
			{
				Pending[Index].mDestination->clear();
			}
		}
		return SubmissionStatus;
	}

	bool FArdaInductorReadbackCompletions::IsReady()
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		return eastl::all_of(mPending.begin(),
		    mPending.end(),
		    [](auto& Completion)
		    {
			    return Completion.mFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		    });
	}

	FArdaRHIStatus FArdaDependencyExecutionContext::ReadbackBuffer(FArdaDependencyResourceHandle Resource,
	    eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
	    uint64_t SourceOffset,
	    uint64_t Size) const
	{
		if (!Destination)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Readback destination is null.");
		}
		if (!mGraph || !mFrame || !mPass || !mGraph->mbExecuting)
		{
			return RuntimeError("Graph readback requires an active node callback.");
		}
		auto Callback =
		    mFrame->mActive->mReadbackCompletions.Register(eastl::move(Destination), mPass->GetPass().GetIndex());
		const auto Buffer = GetBuffer(Resource);
		if (!Buffer)
		{
			return mPass->GetStatus();
		}
		// No callback/promise copy escapes this call except the one owned by the
		// native list (or its submitted completion thread). Failed recording destroys
		// that owner and makes the registered future ready with broken_promise.
		return GetCommands().CopyBufferDeviceToHostAsync(*Buffer, eastl::move(Callback), SourceOffset, Size);
	}

	FArdaRHIDeviceRef FArdaDependencyExecutionContext::GetDevice() const
	{
		return mGraph ? mGraph->mDevice : FArdaRHIDeviceRef{};
	}

	uint32_t FArdaDependencyExecutionContext::GetFrameIndex() const
	{
		return mFrame ? mFrame->mIndex : UINT32_MAX;
	}

	uint64_t FArdaDependencyExecutionContext::GetFrameSequence() const
	{
		return mFrame && mFrame->mActive ? mFrame->mActive->mSequence : 0;
	}

	FArdaRHIBufferRef FArdaDependencyExecutionContext::GetWorkspaceBuffer() const
	{
		if (!mGraph || !mFrame || !mPass)
		{
			return {};
		}
		const auto Found = mGraph->mCompile.mWorkspaceResourceIds.find(mNode.GetIndex());
		if (Found == mGraph->mCompile.mWorkspaceResourceIds.end())
		{
			return {};
		}
		const auto Index = Found->second;
		if (Index >= mFrame->mBuffers.size() || !mFrame->mBuffers[Index])
		{
			mPass->ReportStatus(RuntimeError("A declared workspace has no physical allocation."));
			return {};
		}
		return FArdaRHIBufferRef(mPass->GetBuffer(mFrame->mBuffers[Index]));
	}

	FArdaRHIBufferRef FArdaDependencyExecutionContext::GetBuffer(FArdaDependencyResourceHandle Resource) const
	{
		if (!mGraph || !mFrame || !mPass || !DeclaresResource(*mGraph, mNode, Resource) ||
		    mGraph->mResources[Resource.mIndex].mbTexture || !mFrame->mBuffers[Resource.mIndex])
		{
			if (mPass)
			{
				mPass->ReportStatus(RuntimeError("A node requested an undeclared or unavailable buffer."));
			}
			return {};
		}
		return FArdaRHIBufferRef(mPass->GetBuffer(mFrame->mBuffers[Resource.mIndex]));
	}

	FArdaRHIAccelStructRef FArdaDependencyExecutionContext::GetAccelerationStructure(
	    FArdaDependencyResourceHandle Resource) const
	{
		if (!mGraph || !mFrame || !mPass || !DeclaresResource(*mGraph, mNode, Resource) ||
		    Resource.mIndex >= mFrame->mAccelerationStructures.size() ||
		    !mFrame->mAccelerationStructures[Resource.mIndex])
		{
			if (mPass)
			{
				mPass->ReportStatus(
				    RuntimeError("A node requested an undeclared or unavailable acceleration structure."));
			}
			return {};
		}
		return FArdaRHIAccelStructRef(mPass->GetAccelStruct(mFrame->mAccelerationStructures[Resource.mIndex]));
	}

	FArdaRHITextureRef FArdaDependencyExecutionContext::GetTexture(FArdaDependencyResourceHandle Resource) const
	{
		if (!mGraph || !mFrame || !mPass || !DeclaresResource(*mGraph, mNode, Resource) ||
		    !mGraph->mResources[Resource.mIndex].mbTexture || !mFrame->mTextures[Resource.mIndex])
		{
			if (mPass)
			{
				mPass->ReportStatus(RuntimeError("A node requested an undeclared or unavailable texture."));
			}
			return {};
		}
		return FArdaRHITextureRef(mPass->GetTexture(mFrame->mTextures[Resource.mIndex]));
	}

	const FArdaInductorResolvedPipeline* FArdaDependencyExecutionContext::GetPipeline(const eastl::string& Slot) const
	{
		if (!mGraph || !mFrame)
		{
			return nullptr;
		}
		const auto Node = mFrame->mPipelines.find(mNode.GetIndex());
		if (Node == mFrame->mPipelines.end())
		{
			return nullptr;
		}
		const auto Pipeline = Node->second.find(Slot);
		return Pipeline == Node->second.end() ? nullptr : &Pipeline->second;
	}

	FArdaRHIFramebufferRef FArdaDependencyExecutionContext::GetFramebuffer() const
	{
		if (!mGraph || !mFrame)
		{
			return {};
		}
		const auto Found = mFrame->mFramebuffers.find(mNode.GetIndex());
		return Found == mFrame->mFramebuffers.end() ? FArdaRHIFramebufferRef{} : Found->second;
	}

	FArdaGraphNodeHandle FArdaDependencyExecutionContext::GetNode() const
	{
		return mNode;
	}

	FArdaRHIStatus FArdaInductorRuntime::Prepare(FArdaDependencyGraph::FImpl& Graph,
	    FArdaInductorMemoryPlan Plan,
	    const eastl::vector<FArdaInductorReuseFrame>* Reuse)
	{
		if (!Graph.mDevice)
		{
			return RuntimeError("Device-bound graph preparation requires a device.");
		}
		if (Graph.mRuntime)
		{
			return RuntimeError("Release the previous graph pool before materializing its replacement.");
		}
		if (Plan.mRequests.size() < Graph.mResources.size() || !Graph.mOptions.mFramesInFlight)
		{
			return RuntimeError("The memory plan does not cover the graph resource identifier domain.");
		}
		if (Graph.mCompile.mQueues.size() != Graph.mCompile.mExecutionOrder.size())
		{
			return RuntimeError("Compiled graph queue assignment does not cover its schedule.");
		}
		eastl::unordered_set<uint32_t> SkippedNodes;
		for (auto Handle : Graph.mCompile.mExecutionOrder)
		{
			const auto* Record = Graph.mTopology.TryGetNode(Handle);
			if (!Record)
			{
				return RuntimeError("A compiled node handle is stale.");
			}
			const auto& Node = Record->mPayload;
			if (Node.mDesc.mbPipelineStageOnly)
			{
				if (!Node.mDesc.mAccesses.empty() || !Node.mDesc.mColorTargets.empty() || Node.mDesc.mDepthTarget)
				{
					return RuntimeError("A pipeline-stage-only node cannot access physical resources.");
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
			return RuntimeError("A tuning proposal must reuse every existing frame pool.");
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
			auto Storage = Reuse ? TArdaRHIResult<FArdaInductorMemoryResources>{(*Reuse)[FrameIndex].mMemory, {}}
			                     : MaterializeArdaInductorMemory(*Graph.mDevice,
			                           Plan,
			                           FrameIndex ? &Runtime->mFrames.front()->mMemory : nullptr);
			if (!Storage)
			{
				return Storage.mStatus;
			}
			auto Frame = eastl::make_unique<FArdaInductorFrame>();
			Frame->mIndex = FrameIndex;
			Frame->mMemory = eastl::move(Storage.mValue);
			Frame->mLowered = eastl::make_unique<FArdaInductorCommandProgram>(Graph.mDevice);
			Frame->mBuffers.resize(Plan.mRequests.size());
			Frame->mTextures.resize(Plan.mRequests.size());
			Frame->mAccelerationStructures.resize(Plan.mRequests.size());
			for (size_t Index = 0; Index < Plan.mRequests.size(); ++Index)
			{
				// Retained imports still count toward the memory budget, but resources
				// without scheduled users need no native state contract or command binding.
				if (Plan.mRequests[Index].mFirstUseNodes.empty())
				{
					continue;
				}
				const auto Name = Index < Graph.mResources.size()
				    ? Graph.mResources[Index].mName
				    : eastl::string("Inductor workspace ") + std::to_string(Index).c_str();
				if (Frame->mMemory.mBuffers[Index])
				{
					// External callers hand storage to the graph in its descriptor-declared
					// state on the graphics queue. Owned/persistent pool storage declares Common.
					Frame->mBuffers[Index] = Frame->mLowered->BindBuffer(Frame->mMemory.mBuffers[Index],
					    Reuse ? (*Reuse)[FrameIndex].mBufferStates[Index]
					          : EntryState(Frame->mMemory.mBuffers[Index]->GetDesc().mInitialState),
					    Name);
				}
				if (Frame->mMemory.mAccelerationStructures[Index])
				{
					Frame->mAccelerationStructures[Index] =
					    Frame->mLowered->BindAccelerationStructure(Frame->mMemory.mAccelerationStructures[Index],
					        EArdaRHIResourceState::AccelStructRead,
					        Name);
				}

				if (Frame->mMemory.mTextures[Index])
				{
					auto State = Reuse ? (*Reuse)[FrameIndex].mTextureStates[Index]
					                   : EntryState(Frame->mMemory.mTextures[Index]->GetDesc().mInitialState);
					if (!Reuse && Plan.mRequests[Index].mExternalTexture)
					{
						// Creation descriptors can retain Discard after WSI acquisition has
						// transitioned the image to Present. Preserve the state actually handed
						// to this graph, not the original allocation hint.
						if (!ImportStateQuery)
						{
							auto Created = Graph.mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
							if (!Created)
							{
								return Created.mStatus;
							}
							ImportStateQuery = eastl::move(Created.mValue);
						}
						// State queries are read-only and do not require opening or submitting a list.
						const auto Snapshot = ImportStateQuery->QueryTextureState(*Frame->mMemory.mTextures[Index], {});
						if (!Snapshot)
						{
							return Snapshot.mStatus;
						}
						State = Snapshot.mValue.mFacadeState;
						if (!Snapshot.mValue.IsConsistent() || State == EArdaRHIResourceState::Unknown ||
						    State == EArdaRHIResourceState::Discard)
						{
							return RuntimeError(
							    "Imported textures require an initialized, uniform entry state. Initialize the image or acquire its swap-chain image before compiling the graph.");
						}
						if (Snapshot.mValue.mbFacadeQueueOwnerKnown &&
						    Snapshot.mValue.mFacadeQueueOwner != EArdaRHIQueueType::Graphics)
						{
							return RuntimeError("Imported textures must be handed to the graph on the graphics queue.");
						}
					}
					Frame->mTextures[Index] =
					    Frame->mLowered->BindTexture(Frame->mMemory.mTextures[Index], State, Name);
				}
			}

			if (Reuse)
			{
				Frame->mPipelines = (*Reuse)[FrameIndex].mPipelines;
				Frame->mFramebuffers = (*Reuse)[FrameIndex].mFramebuffers;
			}
			else
			{
				eastl::vector<FArdaInductorPipelineNode> PipelineNodes;
				for (auto Handle : Graph.mTopology.GetNodes())
				{
					FArdaInductorPipelineNode Node;
					Node.mNodeId = Handle.GetIndex();
					Node.mContributions = Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mPipelineStages;
					for (const auto& Request : Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mPipelines)
					{
						Node.mPipelineBoundaries.push_back(Request.mKind);
					}
					for (auto Edge : Graph.mTopology.GetIncomingEdges(Handle))
					{
						Node.mDependencies.push_back(Graph.mTopology.TryGetEdge(Edge)->mFrom.GetIndex());
					}
					PipelineNodes.push_back(eastl::move(Node));
				}
				if (!Graph.mPipelineCache)
				{
					Graph.mPipelineCache = eastl::make_unique<FArdaPipelineStateCache>(Graph.mDevice);
				}
				for (auto Handle : Graph.mCompile.mExecutionOrder)
				{
					if (SkippedNodes.count(Handle.GetIndex()))
					{
						continue;
					}
					const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
					FArdaRHIFramebufferRef Framebuffer;
					if (!Node.mDesc.mColorTargets.empty() || Node.mDesc.mDepthTarget)
					{
						FArdaRHIFramebufferDesc Desc;
						Desc.mDebugName = Node.mName;
						const auto Attachment = [&Node](FArdaDependencyResourceHandle Target, bool bDepth)
						{
							FArdaRHIFramebufferAttachment Result;
							for (const auto& Access : Node.mDesc.mAccesses)
							{
								if (Access.mResource != Target)
								{
									continue;
								}
								if ((!bDepth && Access.mState == EArdaRHIResourceState::RenderTarget) ||
								    (bDepth &&
								        (Access.mState == EArdaRHIResourceState::DepthRead ||
								            Access.mState == EArdaRHIResourceState::DepthWrite)))
								{
									Result.mSubresources = Access.mTextureRange;
									Result.mbReadOnly = bDepth && Access.mState == EArdaRHIResourceState::DepthRead;
									break;
								}
							}
							return Result;
						};
						for (auto Target : Node.mDesc.mColorTargets)
						{
							if (!Graph.HasResource(Target) || !Frame->mMemory.mTextures[Target.mIndex])
							{
								return RuntimeError("A framebuffer target has no materialized texture.");
							}
							Desc.mColorAttachments.push_back(
							    {Frame->mMemory.mTextures[Target.mIndex], Attachment(Target, false)});
						}
						if (Node.mDesc.mDepthTarget)
						{
							const auto Target = Node.mDesc.mDepthTarget;
							if (!Graph.HasResource(Target) || !Frame->mMemory.mTextures[Target.mIndex])
							{
								return RuntimeError("A depth target has no materialized texture.");
							}
							Desc.mDepthAttachment.mTexture = Frame->mMemory.mTextures[Target.mIndex];
							Desc.mDepthAttachment.mAttachment = Attachment(Target, true);
						}
						auto Created = Graph.mDevice->CreateFramebuffer(Desc);
						if (!Created)
						{
							return Created.mStatus;
						}
						Framebuffer = Created.mValue;
						Frame->mFramebuffers.emplace(Handle.GetIndex(), Framebuffer);
					}
					for (auto Request : Node.mDesc.mPipelines)
					{
						Request.mTerminalNodeId = Handle.GetIndex();
						if (Request.mSlot.empty())
						{
							Request.mSlot = "default";
						}
						auto& Slots = Frame->mPipelines[Handle.GetIndex()];
						if (Slots.count(Request.mSlot))
						{
							return RuntimeError("A node requests duplicate pipeline slots.");
						}
						auto Pattern = InferArdaInductorPipeline(PipelineNodes, Request);
						if (!Pattern)
						{
							return Pattern.mStatus;
						}
						auto Resolved = ResolveArdaInductorPipeline(*Graph.mPipelineCache, Pattern.mValue, Framebuffer);
						if (!Resolved)
						{
							return Resolved.mStatus;
						}
						Slots.emplace(Request.mSlot, eastl::move(Resolved.mValue));
					}
				}
			}

			eastl::unordered_map<uint32_t, FArdaInductorCommandHandle> Passes;
			eastl::unordered_map<uint32_t, FArdaInductorCommandHandle> ImageActivations, ImageRetirements;
			eastl::unordered_set<uint32_t> AliasedImages;
			for (const auto& Activation : Plan.mAliasActivations)
			{
				if (Plan.mRequests[Activation.mResource].mKind == EArdaInductorMemoryKind::Texture)
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
				    eastl::string(Activate ? "Activate image " : "Retire image ") + std::to_string(Resource).c_str();
				const auto Pass =
				    Frame->mLowered->AppendCommand(Name, EArdaRHIQueueType::Graphics, eastl::move(Accesses), {}, true);

				if (Activate)
				{
					Frame->mLowered->mImpl->mReusableAliasActivations.push_back(
					    {Pass, EArdaGraphResourceType::Texture, Frame->mTextures[Resource]->GetHandle().GetIndex()});
				}
				return Pass;
			};
			eastl::unordered_map<uint32_t, const eastl::vector<FArdaGraphNodeHandle>*> CudaBatches;
			for (const auto& Batch : Graph.mCompile.mCudaBatches)
			{
				if (!Batch.empty())
				{
					CudaBatches.emplace(Batch.front().GetIndex(), &Batch);
				}
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
							return RuntimeError("An aliased image becomes active before its predecessor retires.");
						}
						Frame->mLowered->AddDependency(Retired->second, Pass);
					}
				}
				if (Passes.count(Head.GetIndex()) || SkippedNodes.count(Head.GetIndex()))
				{
					continue;
				}
				eastl::vector<FArdaGraphNodeHandle> Nodes{Head};
				if (const auto Batch = CudaBatches.find(Head.GetIndex()); Batch != CudaBatches.end())
				{
					Nodes = *Batch->second;
					Nodes.erase(eastl::remove_if(Nodes.begin(),
					                Nodes.end(),
					                [&SkippedNodes](auto Node)
					                {
						                return SkippedNodes.count(Node.GetIndex()) != 0;
					                }),
					    Nodes.end());
				}
				const auto& HeadNode = Graph.mTopology.TryGetNode(Head)->mPayload;
				const bool bCuda = HeadNode.mDefinition->mKind == EArdaDependencyNodeKind::Cuda;
				const auto Queue = Graph.mCompile.mQueues[Position];
				FArdaInductorCommandAccesses Accesses;
				for (auto Handle : Nodes)
				{
					const auto* Record = Graph.mTopology.TryGetNode(Handle);
					if (!Record)
					{
						return RuntimeError("A compiled node handle is stale.");
					}
					const auto& Node = Record->mPayload;
					if (bCuda &&
					    (!Node.mDefinition->mPrepareCuda || Node.mDefinition->mKind != EArdaDependencyNodeKind::Cuda))
					{
						return RuntimeError("A CUDA batch contains an incompatible node definition.");
					}
					for (const auto& Access : Node.mDesc.mAccesses)
					{
						if (!Graph.HasResource(Access.mResource))
						{
							return RuntimeError("A compiled resource handle is stale.");
						}
						const auto Index = Access.mResource.mIndex;
						const bool Write = Access.mAccess != EArdaDependencyAccess::Read;
						if (Graph.mResources[Index].mExternalAccelerationStructure)
						{
							if (!Frame->mAccelerationStructures[Index])
							{
								return RuntimeError("A live acceleration structure has no allocation.");
							}
							Accesses.mAccelerationStructures.push_back(
							    {Frame->mAccelerationStructures[Index]->GetHandle(), Access.mState, Write});
						}
						else if (Graph.mResources[Index].mbTexture)
						{
							if (!Frame->mTextures[Index])
							{
								return RuntimeError("A live texture has no allocation.");
							}
							Accesses.mTextures.push_back(
							    {Frame->mTextures[Index]->GetHandle(), Access.mTextureRange, Access.mState, Write});
						}
						else
						{
							if (!Frame->mBuffers[Index])
							{
								return RuntimeError("A live buffer has no allocation.");
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
							return RuntimeError("A compiled workspace has no allocation.");
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
				const auto Pass = Frame->mLowered->AppendCommand(
				    HeadNode.mName,
				    Queue,
				    eastl::move(Accesses),
				    [Nodes, bCuda, CudaCache, Timer, CudaTimer, Binding = Runtime->mBinding, FramePtr = Frame.get()](
				        FArdaInductorPassContext& PassContext)
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
							    PassContext.ReportStatus(
							        Node.mDefinition->mPrepareCuda(Context, Node.mParameters.get(), Sequence));
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
				    },
				    bCuda,
				    bCuda);
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
							return RuntimeError("An image retirement precedes an actual user.");
						}
						Frame->mLowered->AddDependency(Found->second, Retire);
					}
				}
			}
			const auto AddDependency = [&](FArdaGraphNodeHandle Producer,
			                               FArdaGraphNodeHandle Consumer) -> FArdaRHIStatus
			{
				const auto P = Passes.find(Producer.GetIndex()), C = Passes.find(Consumer.GetIndex());
				if (P == Passes.end() || C == Passes.end() || P->second == C->second)
				{
					return {};
				}
				if (C->second < P->second)
				{
					return RuntimeError("Lowering encountered a dependency against schedule order.");
				}
				Frame->mLowered->AddDependency(P->second, C->second);
				return {};
			};
			eastl::unordered_map<uint32_t, eastl::vector<FArdaGraphNodeHandle>> Predecessors;
			for (auto Handle : Graph.mTopology.GetEdges())
			{
				const auto* Edge = Graph.mTopology.TryGetEdge(Handle);
				Predecessors[Edge->mTo.GetIndex()].push_back(Edge->mFrom);
			}
			for (const auto& Edge : Graph.mCompile.mMemoryDependencies)
			{
				Predecessors[Edge.second.GetIndex()].push_back(Edge.first);
			}
			for (auto Consumer : Graph.mCompile.mExecutionOrder)
			{
				if (!Passes.count(Consumer.GetIndex()))
				{
					continue;
				}
				auto Pending = Predecessors[Consumer.GetIndex()];
				eastl::unordered_set<uint32_t> Visited;
				while (!Pending.empty())
				{
					const auto Producer = Pending.back();
					Pending.pop_back();
					if (!Visited.insert(Producer.GetIndex()).second)
					{
						continue;
					}
					if (SkippedNodes.count(Producer.GetIndex()))
					{
						const auto& Earlier = Predecessors[Producer.GetIndex()];
						Pending.insert(Pending.end(), Earlier.begin(), Earlier.end());
					}
					else if (auto Status = AddDependency(Producer, Consumer); !Status)
					{
						return Status;
					}
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
					return RuntimeError("A placed buffer activation has no physical resource.");
				}
				Frame->mLowered->mImpl->mReusableAliasActivations.push_back(
				    {Consumer->second, EArdaGraphResourceType::Buffer, Frame->mBuffers[Index]->GetHandle().GetIndex()});
			}
			(void)Frame->mLowered->Finalize();
			// Complete the reusable boundary once, before publishing the immutable plan.
			// Equal-state transitions still matter when the last use owned a different
			// queue/family. Physical lowering derives the actual before-state and adds
			// the release/acquire and queue wait; an ordinary state equality is insufficient.
			auto& Lowered = *Frame->mLowered->mImpl;
			auto& Epilogue = Lowered.mPasses.Get(Lowered.mPlan.mEpilogue).GetState();
			eastl::unordered_set<uint32_t> RetiredTextureHandles;
			for (const auto Resource : OrderedAliasedImages)
			{
				if (!ImageRetirements.count(Resource))
				{
					return RuntimeError("An aliased image has no retirement boundary.");
				}
				RetiredTextureHandles.insert(Frame->mTextures[Resource]->GetHandle().GetIndex());
			}
			Epilogue.mTextureTransitions.erase(eastl::remove_if(Epilogue.mTextureTransitions.begin(),
			                                       Epilogue.mTextureTransitions.end(),
			                                       [&](const auto& Transition)
			                                       {
				                                       return RetiredTextureHandles.count(
				                                                  Transition.mTexture.GetIndex()) != 0;
			                                       }),
			    Epilogue.mTextureTransitions.end());
			for (const auto* Buffer : Lowered.mBuffers.GetEntries())
			{
				if (Buffer->GetFirstUse().IsValid() && Buffer->GetFirstUse() != Lowered.mPlan.mEpilogue &&
				    eastl::none_of(Epilogue.mBufferTransitions.begin(),
				        Epilogue.mBufferTransitions.end(),
				        [Buffer](const auto& Transition)
				        {
					        return Transition.mBuffer == Buffer->GetHandle();
				        }))
				{
					Epilogue.mBufferTransitions.push_back(
					    {Buffer->GetHandle(), Buffer->GetFinalState(), Buffer->GetFinalState()});
				}
			}
			for (const auto* Texture : Lowered.mTextures.GetEntries())
			{
				if (RetiredTextureHandles.count(Texture->GetHandle().GetIndex()))
				{
					continue;
				}
				if (!Texture->GetFirstUse().IsValid() || Texture->GetFirstUse() == Lowered.mPlan.mEpilogue)
				{
					continue;
				}
				const auto& Desc = Texture->GetDesc();
				VisitInductorTextureCells(Desc,
				    {},
				    [&](const auto& Cell, size_t)
				    {
					    const bool Present = eastl::any_of(Epilogue.mTextureTransitions.begin(),
					        Epilogue.mTextureTransitions.end(),
					        [Texture, &Cell](const auto& Transition)
					        {
						        return Transition.mTexture == Texture->GetHandle() && Transition.mSubresources == Cell;
					        });
					    if (!Present)
					    {
						    Epilogue.mTextureTransitions.push_back(
						        {Texture->GetHandle(), Cell, Texture->GetFinalState(), Texture->GetFinalState()});
					    }
				    });
			}
			Runtime->mFrames.push_back(eastl::move(Frame));
		}
		if (Graph.mOptions.mbEnableGpuTiming)
		{
			Runtime->mAdaptivePlan = eastl::make_shared<const FArdaInductorMemoryPlan>(Plan);
			auto Reusable = eastl::make_shared<eastl::vector<FArdaInductorReuseFrame>>();
			for (const auto& Frame : Runtime->mFrames)
			{
				FArdaInductorReuseFrame R;
				R.mMemory = Frame->mMemory;
				R.mPipelines = Frame->mPipelines;
				R.mFramebuffers = Frame->mFramebuffers;
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

	FArdaGraphExecutionResult FArdaInductorRuntime::WaitState(FArdaDependencyFrameTicket::FState& State)
	{
		std::lock_guard<std::mutex> Lock(State.mMutex);
		if (State.mbComplete)
		{
			return State.mExecution;
		}
		for (const auto Instance : State.mExecution.mLastSubmittedInstances)
		{
			if (!Instance)
			{
				continue;
			}
			const auto Status = State.mDevice->WaitForSubmission(Instance);
			if (!Status && State.mRetirementStatus)
			{
				State.mRetirementStatus = Status;
			}
		}
		for (const auto& Fence : State.mRecoveryFences)
		{
			const auto Status = State.mDevice->WaitGpuFence(Fence);
			if (!Status && State.mRetirementStatus)
			{
				State.mRetirementStatus = Status;
			}
		}
		if (State.mExecution.mStatus && !State.mRetirementStatus)
		{
			State.mExecution.mStatus = State.mRetirementStatus;
		}
		CollectStateTelemetry(State);

		// Only the native completion callback owns each pending promise. Unsubmitted
		// command lists were destroyed before Submit returned, so their waits cancel.
		State.mExecution.mStatus = State.mReadbackCompletions.Drain(State.mExecution.mStatus);
		if (!State.mExecution.mStatus)
		{
			State.mTimers.clear();
			State.mCudaTimers.clear();
		}
		State.mRecoveryFences.clear();
		State.mbComplete = true;
		return State.mExecution;
	}

	FArdaInductorRuntime::~FArdaInductorRuntime()
	{
		(void)WaitAll();
	}

	FArdaRHIStatus FArdaInductorRuntime::WaitAll()
	{
		FArdaRHIStatus Status;
		eastl::vector<eastl::shared_ptr<FArdaDependencyFrameTicket::FState>> Pending;
		for (const auto& Frame : mFrames)
		{
			if (Frame->mActive)
			{
				Pending.push_back(Frame->mActive);
			}
		}
		// Pool wraparound changes slot order. Automatic retirement still publishes
		// pending readbacks in frame order when several frames share a destination.
		eastl::sort(Pending.begin(),
		    Pending.end(),
		    [](const auto& A, const auto& B)
		    {
			    return A->mSequence < B->mSequence;
		    });
		for (const auto& State : Pending)
		{
			(void)WaitState(*State);
			// A recording/readback error must not prevent editing to repair the graph.
			if (Status && !State->mRetirementStatus)
			{
				Status = State->mRetirementStatus;
			}
		}
		return Status;
	}

	TArdaRHIResult<FArdaDependencyFrameTicket> FArdaInductorRuntime::Submit(FArdaDependencyGraph::FImpl& Graph,
	    const FArdaGraphExecuteOptions& Options)
	{
		if (Graph.mbEditing || Graph.mbExecuting || !Graph.mRuntime || Graph.mRuntime->mFrames.empty())
		{
			return {{}, RuntimeError("Submit requires a successfully compiled graph outside an edit.")};
		}

		AdvanceArdaInductorTelemetry(Graph);

		struct FExecutionGuard
		{
			bool& mExecuting;

			explicit FExecutionGuard(bool& Executing)
			    : mExecuting(Executing)
			{
				mExecuting = true;
			}

			~FExecutionGuard()
			{
				mExecuting = false;
			}
		} Guard(Graph.mbExecuting);

		auto& Runtime = *Graph.mRuntime;
		auto& Frame = *Runtime.mFrames[Runtime.mNextSlot];
		if (Frame.mActive)
		{
			const auto Previous = WaitState(*Frame.mActive);
			if (!Previous.mStatus)
			{
				return {{}, Previous.mStatus};
			}
		}
		if (Runtime.mPrevious && !Runtime.mPrevious->mExecution.mStatus)
		{
			return {{}, RuntimeError("Repair or recompile the graph after a failed frame submission.")};
		}
		auto State = eastl::make_shared<FArdaDependencyFrameTicket::FState>();
		State->mDevice = Graph.mDevice;
		State->mGraphIdentity = Graph.mIdentity;
		State->mSequence = Graph.mNextFrameSequence++;
		State->mTiming = Graph.mTiming;
		State->mbSampleTiming =
		    Graph.mOptions.mbEnableGpuTiming && (State->mSequence - 1) % Graph.mOptions.mGpuTimingSampleInterval == 0;
		if (State->mbSampleTiming && Frame.mTimingState)
		{
			std::unique_lock<std::mutex> Lock(Frame.mTimingState->mMutex, std::try_to_lock);
			State->mbSampleTiming =
			    Lock.owns_lock() && Frame.mTimingState->mTimers.empty() && Frame.mTimingState->mCudaTimers.empty();
		}
		State->mTimingEpoch = State->mbSampleTiming ? Graph.mTiming->GetEpoch() : 0;
		if (State->mbSampleTiming)
		{
			State->mTimers = Frame.mTimers;
			State->mCudaTimers = Frame.mCudaTimers;
			Frame.mTimingState = State;
		}
		Frame.mActive = State;
		FArdaDependencyFrameTicket Ticket;
		Ticket.mState = State;
		uint32_t InterFrameWaits = 0;
		try
		{
			for (const auto& Timer : State->mTimers)
			{
				if (!Graph.mDevice->ResetTimerQuery(Timer.mQuery))
				{
					State->mbSampleTiming = false;
					break;
				}
			}
			if (!State->mbSampleTiming)
			{
				State->mTimers.clear();
				State->mCudaTimers.clear();
			}
			// A shared object's entry/exit state also carries ownership. Even shared
			// reads are serialized conservatively across frames when queues can differ.
			if (State->mExecution.mStatus && Runtime.mbHasSharedResources && Runtime.mPrevious)
			{
				eastl::array<bool, ArdaRHIQueueTypeCount> UsedQueues{};
				UsedQueues[GetArdaRHIQueueIndex(EArdaRHIQueueType::Graphics)] = true;
				for (const auto Queue : Graph.mCompile.mQueues)
				{
					UsedQueues[GetArdaRHIQueueIndex(Queue)] = true;
				}
				for (uint32_t Consumer = 0; Consumer < UsedQueues.size() && State->mExecution.mStatus; ++Consumer)
				{
					if (!UsedQueues[Consumer])
					{
						continue;
					}
					for (uint32_t Producer = 0; Producer < ArdaRHIQueueTypeCount; ++Producer)
					{
						const auto Instance = Runtime.mPrevious->mExecution.mLastSubmittedInstances[Producer];
						if (!Instance || Producer == Consumer)
						{
							continue;
						}
						State->mExecution.mStatus = Graph.mDevice->QueueWait(static_cast<EArdaRHIQueueType>(Consumer),
						    static_cast<EArdaRHIQueueType>(Producer),
						    Instance);
						if (!State->mExecution.mStatus)
						{
							break;
						}
						++InterFrameWaits;
					}
				}
			}
			if (State->mExecution.mStatus)
			{
				State->mExecution = FArdaInductorCommandExecutor::Submit(*Frame.mLowered, Options);
			}
		}
		catch (const std::exception& Error)
		{
			State->mExecution = Frame.mLowered->mImpl->mExecutionResult;
			State->mExecution.mStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
		}
		catch (...)
		{
			State->mExecution = Frame.mLowered->mImpl->mExecutionResult;
			State->mExecution.mStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			    "Graph recording or submission threw an exception.");
		}
		State->mExecution.mQueueWaitCount += InterFrameWaits;
		// If a provider throws after accepting work without returning a token, mark
		// only that queue's current tail. Never turn graph retirement into device idle.
		if (Frame.mLowered->mImpl->mSubmittingQueue >= 0)
		{
			auto Fence = Graph.mDevice->CreateGpuFence();
			if (Fence)
			{
				const auto Status = Graph.mDevice->SignalGpuFence(Fence.mValue,
				    static_cast<EArdaRHIQueueType>(Frame.mLowered->mImpl->mSubmittingQueue));
				if (Status)
				{
					State->mRecoveryFences.push_back(eastl::move(Fence.mValue));
				}
				else
				{
					State->mRetirementStatus = Status;
				}
			}
			else
			{
				State->mRetirementStatus = Fence.mStatus;
			}
		}
		Runtime.mPrevious = State;
		Runtime.mNextSlot = (Runtime.mNextSlot + 1) % Runtime.mFrames.size();
		return {eastl::move(Ticket), State->mExecution.mStatus};
	}

	FArdaGraphExecutionResult FArdaInductorRuntime::Wait(FArdaDependencyGraph::FImpl& Graph,
	    const FArdaDependencyFrameTicket& Ticket)
	{
		if (Graph.mbExecuting || !Ticket.mState || Ticket.mState->mGraphIdentity != Graph.mIdentity)
		{
			FArdaGraphExecutionResult Result;
			Result.mStatus = RuntimeError("Wait requires this graph's frame ticket outside a callback.");
			return Result;
		}
		return WaitState(*Ticket.mState);
	}

	TArdaRHIResult<bool> FArdaInductorRuntime::IsComplete(FArdaDependencyGraph::FImpl& Graph,
	    const FArdaDependencyFrameTicket& Ticket)
	{
		if (Graph.mbExecuting || !Ticket.mState || Ticket.mState->mGraphIdentity != Graph.mIdentity)
		{
			return {false, RuntimeError("IsComplete requires this graph's frame ticket outside a callback.")};
		}
		auto& State = *Ticket.mState;
		std::unique_lock<std::mutex> Lock(State.mMutex, std::try_to_lock);
		if (!Lock.owns_lock())
		{
			return {false, {}};
		}
		if (State.mbComplete)
		{
			return {true, {}};
		}
		for (const auto Instance : State.mExecution.mLastSubmittedInstances)
		{
			if (!Instance)
			{
				continue;
			}
			const auto Ready = State.mDevice->PollSubmission(Instance);
			if (!Ready || !Ready.mValue)
			{
				return Ready;
			}
		}
		for (const auto& Fence : State.mRecoveryFences)
		{
			const auto Ready = State.mDevice->PollGpuFence(Fence);
			if (!Ready || !Ready.mValue)
			{
				return Ready;
			}
		}
		return {State.mReadbackCompletions.IsReady(), {}};
	}

	FArdaGraphExecutionResult FArdaInductorRuntime::Execute(FArdaDependencyGraph::FImpl& Graph,
	    const FArdaGraphExecuteOptions& Options)
	{
		auto Submitted = Submit(Graph, Options);
		if (Submitted.mValue)
		{
			return Wait(Graph, Submitted.mValue);
		}
		FArdaGraphExecutionResult Result;
		Result.mStatus = Submitted.mStatus;
		return Result;
	}
}
