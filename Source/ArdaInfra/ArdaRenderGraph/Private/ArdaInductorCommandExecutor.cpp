#include "ArdaInductorPch.h"
#include "ArdaInductorState.h"
#include "ArdaInductorCommandProgramInternal.h"
#include "ArdaInductorCommandExecutor.h"
#include "ArdaInductorLog.h"
#include "ArdaScopeTimer.h"
#include "ArdaTrace.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>
#include <EASTL/type_traits.h>
#include <future>
#include <thread>

namespace arda
{
	namespace
	{
		struct FArdaInductorRuntimeTransitions
		{
			struct FArdaAliasingResource
			{
				EArdaGraphResourceType mType = EArdaGraphResourceType::Texture;
				uint32_t mResourceIndex = 0;
				bool mbDiscardTexture = false;
			};

			struct FArdaTextureQueueTransfer
			{
				FArdaInductorTextureHandle mTexture;
				arda::FArdaRHITextureSubresourceRange mSubresources;
				arda::EArdaRHIQueueType mSourceQueue = arda::EArdaRHIQueueType::Graphics;
				arda::EArdaRHIQueueType mDestinationQueue = arda::EArdaRHIQueueType::Graphics;
			};

			struct FArdaBufferQueueTransfer
			{
				FArdaInductorBufferHandle mBuffer;
				arda::EArdaRHIQueueType mSourceQueue = arda::EArdaRHIQueueType::Graphics;
				arda::EArdaRHIQueueType mDestinationQueue = arda::EArdaRHIQueueType::Graphics;
			};

			/** Physical texture transitions emitted while recording this pass. */
			eastl::vector<FArdaInductorTextureTransition> mTextures;
			/** Physical buffer transitions emitted while recording this pass. */
			eastl::vector<FArdaInductorBufferTransition> mBuffers;
			eastl::vector<FArdaInductorAccelerationStructureTransition> mAccelStructs;
			/** Queue-ownership acquire barriers emitted before pass transitions. */
			eastl::vector<FArdaTextureQueueTransfer> mTextureAcquires;
			eastl::vector<FArdaBufferQueueTransfer> mBufferAcquires;
			/** Common-state releases emitted after pass work on the producer queue. */
			eastl::vector<FArdaTextureQueueTransfer> mTextureReleases;
			eastl::vector<FArdaBufferQueueTransfer> mBufferReleases;
			/** Placed resources whose overlapping heap range becomes active in this pass. */
			eastl::vector<FArdaAliasingResource> mAliasingResources;
		};

		struct FArdaInductorRecordedCommand
		{
			/** First failure from command recording or the pass callback. */
			arda::FArdaRHIStatus mStatus;
			/** RHI command list populated during the recording stage. */
			arda::FArdaRHICommandListRef mCommandList;
			/** Submission queue selected from the pass pipeline; graphics is the default. */
			arda::EArdaRHIQueueType mQueue = arda::EArdaRHIQueueType::Graphics;
			/** State evidence captured while recording this pass. */
			eastl::vector<FArdaGraphStateConformanceRecord> mStateConformanceRecords;
		};

		struct FArdaInductorRecordingFailureGuard
		{
			/** Non-owning graph implementation protected for this execution attempt. */
			FArdaInductorCommandProgram::FArdaImpl& mGraph;
			/** Disarmed only after every command has been accepted. */
			bool mbCompleted = false;

			/**
             * Makes an interrupted frame slot terminal until the graph is repaired.
             *
             * Submission disarms the guard only after all commands are accepted,
             * so exceptions cannot leave a
             * partially submitted graph looking reusable.
             */
			~FArdaInductorRecordingFailureGuard()
			{
				if (!mbCompleted)
				{
					mGraph.mbFailed = true;
				}
			}
		};

		[[nodiscard]] arda::EArdaRHIPipeline GetTransitionPipeline(arda::EArdaRHIQueueType Queue) noexcept
		{
			switch (Queue)
			{
			case arda::EArdaRHIQueueType::Compute:
				return arda::EArdaRHIPipeline::AsyncCompute;
			case arda::EArdaRHIQueueType::Copy:
				return arda::EArdaRHIPipeline::Copy;
			case arda::EArdaRHIQueueType::Graphics:
			default:
				return arda::EArdaRHIPipeline::Graphics;
			}
		}

		template <class Resource>
		void CaptureResourceState(FArdaInductorRecordedCommand& Recorded,
		    const FArdaInductorCommand& Pass,
		    FArdaInductorCommandHandle PassHandle,
		    const Resource& Value,
		    EArdaGraphStateCheckpoint Checkpoint,
		    EArdaRHIResourceState ExpectedState,
		    const FArdaRHITextureSubresourceRange& Subresources = {},
		    bool bValidateQueueOwnership = false,
		    EArdaRHIQueueType ExpectedQueueOwner = EArdaRHIQueueType::Graphics,
		    uint32_t ExpectedQueueFamily = ArdaRHIInvalidQueueFamily)
		{
			constexpr bool bTexture = eastl::is_same_v<Resource, FArdaInductorTexture>;
			FArdaGraphStateConformanceRecord Record;
			Record.mPass = PassHandle.GetIndex();
			Record.mPassName = Pass.GetName();
			Record.mResourceType = bTexture ? EArdaGraphResourceType::Texture : EArdaGraphResourceType::Buffer;
			Record.mResourceIndex = Value.GetHandle().GetIndex();
			Record.mResourceName = Value.GetName();
			Record.mTextureSubresources = Subresources;
			Record.mCheckpoint = Checkpoint;
			Record.mExpectedState = ExpectedState;
			Record.mbValidateQueueOwnership = bValidateQueueOwnership;
			Record.mExpectedQueueOwner = ExpectedQueueOwner;
			Record.mExpectedQueueFamily = ExpectedQueueFamily;
			auto Snapshot = [&]
			{
				if constexpr (eastl::is_same_v<Resource, FArdaInductorTexture>)
				{
					return Recorded.mCommandList->QueryTextureState(*Value.GetResource(), Subresources);
				}
				else
				{
					return Recorded.mCommandList->QueryBufferState(*Value.GetResource());
				}
			}();
			Record.mStatus = Snapshot.mStatus;
			if (Snapshot)
			{
				Record.mObserved = eastl::move(Snapshot.mValue);
			}
			Recorded.mStateConformanceRecords.push_back(eastl::move(Record));
		}

		template <class Transfer>
		FArdaRHIStatus RecordQueueTransfer(FArdaInductorCommandProgram::FArdaImpl& Graph,
		    FArdaInductorRecordedCommand& Recorded,
		    const FArdaInductorCommand& Pass,
		    FArdaInductorCommandHandle Handle,
		    const Transfer& Handoff,
		    bool bAcquire,
		    bool bValidateResourceStates)
		{
			constexpr bool bTexture =
			    eastl::is_same_v<Transfer, FArdaInductorRuntimeTransitions::FArdaTextureQueueTransfer>;
			const auto& Resource = [&]() -> const auto&
			{
				if constexpr (eastl::is_same_v<Transfer, FArdaInductorRuntimeTransitions::FArdaTextureQueueTransfer>)
				{
					return Graph.mTextures.Get(Handoff.mTexture);
				}
				else
				{
					return Graph.mBuffers.Get(Handoff.mBuffer);
				}
			}();
			auto& Commands = *Recorded.mCommandList;
			eastl::conditional_t<bTexture, FArdaRHITextureTransitionDesc, FArdaRHIBufferTransitionDesc> Transition;
			FArdaRHITextureSubresourceRange Subresources;
			FArdaRHIStatus Status;
			if constexpr (bTexture)
			{
				Subresources = Transition.mSubresources = Handoff.mSubresources;
				Status = bAcquire
				    ? Commands.BeginTrackingTextureState(*Resource.GetResource(),
				          Subresources,
				          EArdaRHIResourceState::Common)
				    : Commands.SetTextureState(*Resource.GetResource(), Subresources, EArdaRHIResourceState::Common);
			}
			else
			{
				Status = bAcquire
				    ? Commands.BeginTrackingBufferState(*Resource.GetResource(), EArdaRHIResourceState::Common)
				    : Commands.SetBufferState(*Resource.GetResource(), EArdaRHIResourceState::Common);
			}
			if (!Status)
			{
				return Status;
			}
			if (!bAcquire)
			{
				Commands.CommitBarriers();
			}
			Transition.mStateBefore = Transition.mStateAfter = EArdaRHIResourceState::Common;
			Transition.mSourcePipelines = GetTransitionPipeline(Handoff.mSourceQueue);
			Transition.mDestinationPipelines = GetTransitionPipeline(Handoff.mDestinationQueue);
			Transition.mFlags = bAcquire ? EArdaRHITransitionFlags::EndOnly : EArdaRHITransitionFlags::BeginOnly;
			Transition.mSourceQueue = Handoff.mSourceQueue;
			Transition.mDestinationQueue = Handoff.mDestinationQueue;
			Transition.mbQueueOwnershipTransfer = true;
			if constexpr (bTexture)
			{
				Status = Commands.TransitionTexture(*Resource.GetResource(), Transition);
			}
			else
			{
				Status = Commands.TransitionBuffer(*Resource.GetResource(), Transition);
			}
			if (Status && bValidateResourceStates)
			{
				CaptureResourceState(Recorded,
				    Pass,
				    Handle,
				    Resource,
				    bAcquire ? EArdaGraphStateCheckpoint::QueueAcquire : EArdaGraphStateCheckpoint::QueueRelease,
				    EArdaRHIResourceState::Common,
				    Subresources,
				    true,
				    Handoff.mDestinationQueue,
				    Graph.mDevice->GetCapabilities().mQueues.GetFamily(Handoff.mDestinationQueue));
			}
			return Status;
		}

		void AddExecutionDependency(FArdaInductorCommandProgram::FArdaImpl& Graph,
		    FArdaInductorCommandHandle Producer,
		    FArdaInductorCommandHandle Consumer)
		{
			const auto Source = Graph.mPasses.Get(Producer).GetState().mQueue;
			const auto Destination = Graph.mPasses.Get(Consumer).GetState().mQueue;
			auto& Edges = Graph.mExecutionResult.mQueueDependencies;
			if (Source != Destination &&
			    eastl::none_of(Edges.begin(),
			        Edges.end(),
			        [Producer, Consumer](const auto& Edge)
			        {
				        return Edge.mProducer == Producer.GetIndex() && Edge.mConsumer == Consumer.GetIndex();
			        }))
			{
				Edges.push_back({Producer.GetIndex(), Consumer.GetIndex(), Source, Destination});
			}
		}

		[[nodiscard]] eastl::vector<FArdaInductorRuntimeTransitions> BuildPhysicalTransitions(
		    FArdaInductorCommandProgram::FArdaImpl& Graph)
		{
			struct FArdaTextureQueueHistory
			{
				eastl::vector<FArdaInductorCommandHandle> mPasses;
				eastl::vector<arda::EArdaRHIQueueType> mQueues;
				eastl::vector<FArdaInductorTextureHandle> mTextures;
			};

			struct FArdaBufferQueueHistory
			{
				FArdaInductorCommandHandle mPass;
				arda::EArdaRHIQueueType mQueue = arda::EArdaRHIQueueType::Graphics;
				FArdaInductorBufferHandle mBuffer;
			};

			eastl::unordered_map<const void*, eastl::vector<arda::EArdaRHIResourceState>> TextureStates;
			eastl::unordered_map<const void*, FArdaTextureQueueHistory> TextureQueues;
			eastl::unordered_map<const void*, arda::EArdaRHIResourceState> BufferStates;
			eastl::unordered_map<const void*, FArdaBufferQueueHistory> BufferQueues;
			// Outstanding accesses since the last memory/state barrier, by physical allocation.
			eastl::unordered_map<const void*, eastl::vector<FArdaInductorBufferAccess>> PendingBufferAccesses;
			eastl::vector<FArdaInductorRuntimeTransitions> Runtime(Graph.mPasses.GetCount());
			for (FArdaInductorCommandHandle Handle : Graph.mPlan.mExecutionOrder)
			{
				const FArdaInductorCommand& Pass = Graph.mPasses.Get(Handle);
				auto& Out = Runtime[Handle.GetIndex()];
				const arda::EArdaRHIQueueType Queue = Pass.GetState().mQueue;
				for (const FArdaInductorTextureTransition& Compiled : Pass.GetState().mTextureTransitions)
				{
					const FArdaInductorTexture& Texture = Graph.mTextures.Get(Compiled.mTexture);
					const void* Physical = Texture.GetResource()->GetPhysicalIdentity();
					if (Physical == nullptr)
					{
						ARDA_CHECK_MSG("A live graph texture was not materialized.");
					}
					auto& States = TextureStates[Physical];
					const arda::FArdaRHITextureDesc& Desc = Texture.GetDesc();
					if (States.empty())
					{
						// A physical texture enters history once; later logical
						// aliases continue from the state left in this vector.
						States.resize(GetInductorTextureStateCount(Desc),
						    NormalizeInitialState(Texture.GetInitialState()));
						auto& History = TextureQueues[Physical];
						History.mPasses.assign(States.size(), Graph.mPlan.mPrologue);
						History.mQueues.assign(States.size(), arda::EArdaRHIQueueType::Graphics);
						History.mTextures.assign(States.size(), Compiled.mTexture);
					}
					auto& History = TextureQueues[Physical];
					VisitInductorTextureCells(Desc,
					    Compiled.mSubresources,
					    [&](const auto& Cell, size_t Index)
					    {
						    if (History.mQueues[Index] != Queue)
						    {
							    AddExecutionDependency(Graph, History.mPasses[Index], Handle);
							    const auto Transfer =
							        FArdaInductorRuntimeTransitions::FArdaTextureQueueTransfer{History.mTextures[Index],
							            Cell,
							            History.mQueues[Index],
							            Queue};
							    Runtime[History.mPasses[Index].GetIndex()].mTextureReleases.push_back(Transfer);
							    Out.mTextureAcquires.push_back(
							        {Compiled.mTexture, Cell, History.mQueues[Index], Queue});
							    States[Index] = arda::EArdaRHIResourceState::Common;
						    }
						    Out.mTextures.push_back({Compiled.mTexture,
						        Cell,
						        States[Index],
						        Compiled.mStateAfter,
						        States[Index] == Compiled.mStateAfter && IsUAVState(Compiled.mStateAfter)});
						    States[Index] = Compiled.mStateAfter;
						    History.mPasses[Index] = Handle;
						    History.mQueues[Index] = Queue;
						    History.mTextures[Index] = Compiled.mTexture;
					    });
				}

				for (const FArdaInductorBufferTransition& Compiled : Pass.GetState().mBufferTransitions)
				{
					const FArdaInductorBuffer& Buffer = Graph.mBuffers.Get(Compiled.mBuffer);
					const void* Physical = Buffer.GetResource()->GetPhysicalIdentity();
					if (Physical == nullptr)
					{
						ARDA_CHECK_MSG("A live graph buffer was not materialized.");
					}
					auto Existing = BufferStates.find(Physical);
					if (Existing == BufferStates.end())
					{
						Existing =
						    BufferStates.emplace(Physical, NormalizeInitialState(Buffer.GetInitialState())).first;
						BufferQueues.emplace(Physical,
						    FArdaBufferQueueHistory{Graph.mPlan.mPrologue,
						        arda::EArdaRHIQueueType::Graphics,
						        Compiled.mBuffer});
					}
					auto& History = BufferQueues.at(Physical);
					if (History.mQueue != Queue)
					{
						AddExecutionDependency(Graph, History.mPass, Handle);
						Runtime[History.mPass.GetIndex()].mBufferReleases.push_back(
						    {History.mBuffer, History.mQueue, Queue});
						Out.mBufferAcquires.push_back({Compiled.mBuffer, History.mQueue, Queue});
						Existing->second = arda::EArdaRHIResourceState::Common;
					}
					auto& Pending = PendingBufferAccesses[Physical];
					const bool SameUAV = Existing->second == Compiled.mStateAfter && IsUAVState(Compiled.mStateAfter);
					bool UAVBarrier = SameUAV && Pass.GetState().mbSentinel;
					if (SameUAV)
					{
						// An imported UAV's first access must order work preceding this graph.
						UAVBarrier |= History.mPass == Graph.mPlan.mPrologue;
						for (const auto& A : Pass.GetState().mBufferStates)
						{
							if (A.mBuffer != Compiled.mBuffer)
							{
								continue;
							}
							const auto X = A.mRange.Resolve(Buffer.GetDesc());
							for (const auto& B : Pending)
							{
								const auto Y = B.mRange.Resolve(Buffer.GetDesc());
								UAVBarrier |= (A.mbWrite || B.mbWrite) && X.mByteOffset < Y.mByteOffset + Y.mByteSize &&
								    Y.mByteOffset < X.mByteOffset + X.mByteSize;
							}
						}
					}
					if (!SameUAV || UAVBarrier)
					{
						Pending.clear();
					}
					for (const auto& A : Pass.GetState().mBufferStates)
					{
						if (A.mBuffer == Compiled.mBuffer)
						{
							Pending.push_back(A);
						}
					}
					Out.mBuffers.push_back({Compiled.mBuffer, Existing->second, Compiled.mStateAfter, UAVBarrier});
					Existing->second = Compiled.mStateAfter;
					History = {Handle, Queue, Compiled.mBuffer};
				}
				for (const FArdaInductorAccelerationStructureTransition& Compiled :
				    Pass.GetState().mAccelStructTransitions)
				{
					Out.mAccelStructs.push_back(Compiled);
				}
			}

			return Runtime;
		}

		[[nodiscard]] FArdaInductorRecordedCommand RecordPass(FArdaInductorCommandProgram& Program,
		    FArdaInductorCommandProgram::FArdaImpl& Graph,
		    FArdaInductorCommandHandle Handle,
		    const FArdaInductorRuntimeTransitions& Transitions,
		    bool bValidateResourceStates,
		    FArdaInductorRecordedCommand Recorded = {},
		    bool bClose = true)
		{
			FArdaInductorCommand& Pass = Graph.mPasses.Get(Handle);
			const auto Accept = [&Recorded](FArdaRHIStatus Status)
			{
				if (!Status)
				{
					if (Recorded.mStatus)
					{
						Recorded.mStatus = eastl::move(Status);
					}
					return false;
				}
				return true;
			};
			if (!Recorded.mCommandList)
			{
				Recorded.mQueue = Pass.GetState().mQueue;
				auto CommandListResult = Graph.mDevice->CreateCommandList(Recorded.mQueue);
				if (!CommandListResult)
				{
					Recorded.mStatus = CommandListResult.mStatus;
					return Recorded;
				}

				Recorded.mCommandList = eastl::move(CommandListResult.mValue);
				Recorded.mStatus = Recorded.mCommandList->Open();
				if (!Recorded.mStatus)
				{
					return Recorded;
				}
				Recorded.mCommandList->SetAutomaticBarriers(false);
				for (const auto& Alias : Transitions.mAliasingResources)
				{
					arda::IArdaRHIResource* ResourceAfter = nullptr;
					if (Alias.mType == EArdaGraphResourceType::Texture)
					{
						ResourceAfter =
						    Graph.mTextures.Get(FArdaInductorTextureHandle(Alias.mResourceIndex)).GetResource().Get();
					}
					else if (Alias.mType == EArdaGraphResourceType::Buffer)
					{
						ResourceAfter =
						    Graph.mBuffers.Get(FArdaInductorBufferHandle(Alias.mResourceIndex)).GetResource().Get();
					}
					if (!ResourceAfter)
					{
						Recorded.mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
						    "An Inductor alias activation has no physical resource.");
						return Recorded;
					}
					if (!Accept(Recorded.mCommandList->AliasingBarrier(nullptr, ResourceAfter)))
					{
						return Recorded;
					}
					if (Alias.mbDiscardTexture)
					{
						Recorded.mCommandList->CommitBarriers();
						auto& Texture = Graph.mTextures.Get(FArdaInductorTextureHandle(Alias.mResourceIndex));
						if (!Accept(Recorded.mCommandList->BeginTrackingTextureState(*Texture.GetResource(),
						        {},
						        arda::EArdaRHIResourceState::Common)))
						{
							return Recorded;
						}
						arda::FArdaRHITextureTransitionDesc Activate;
						Activate.mStateBefore = arda::EArdaRHIResourceState::Common;
						Activate.mStateAfter = arda::EArdaRHIResourceState::Common;
						Activate.mFlags = arda::EArdaRHITransitionFlags::Discard;
						Recorded.mStatus = Recorded.mCommandList->TransitionTexture(*Texture.GetResource(), Activate);
						if (!Recorded.mStatus)
						{
							return Recorded;
						}
					}
				}
				if (!Transitions.mAliasingResources.empty())
				{
					Recorded.mCommandList->CommitBarriers();
				}

				// Queue handoffs use Common as the portable ownership state.
				// The producer records a release after its work; the matching
				// acquire executes here before the consumer's normal state
				// transition. This is required by D3D12 copy queues and maps
				// directly to Vulkan queue-family ownership barriers.
				for (const auto& Transfer : Transitions.mTextureAcquires)
				{
					if (!Accept(RecordQueueTransfer(Graph,
					        Recorded,
					        Pass,
					        Handle,
					        Transfer,
					        true,
					        bValidateResourceStates)))
					{
						return Recorded;
					}
				}
				for (const auto& Transfer : Transitions.mBufferAcquires)
				{
					if (!Accept(RecordQueueTransfer(Graph,
					        Recorded,
					        Pass,
					        Handle,
					        Transfer,
					        true,
					        bValidateResourceStates)))
					{
						return Recorded;
					}
				}

				// Runtime records, rather than compiled logical before-states,
				// are authoritative after physical pooling has been resolved.
				for (const FArdaInductorTextureTransition& Transition : Transitions.mTextures)
				{
					FArdaInductorTexture& Texture = Graph.mTextures.Get(Transition.mTexture);
					if (!Accept(Recorded.mCommandList->BeginTrackingTextureState(*Texture.GetResource(),
					        Transition.mSubresources,
					        Transition.mStateBefore)))
					{
						return Recorded;
					}
					if (bValidateResourceStates)
					{
						CaptureResourceState(Recorded,
						    Pass,
						    Handle,
						    Texture,
						    EArdaGraphStateCheckpoint::BeforeTransition,
						    Transition.mStateBefore,
						    Transition.mSubresources);
					}

					if (IsUAVState(Transition.mStateAfter))
					{
						if (!Accept(Recorded.mCommandList->SetUAVBarriersForTexture(*Texture.GetResource(), true)))
						{
							return Recorded;
						}
					}
					if (!Accept(Recorded.mCommandList->SetTextureState(*Texture.GetResource(),
					        Transition.mSubresources,
					        Transition.mStateAfter)))
					{
						return Recorded;
					}
					if (bValidateResourceStates)
					{
						CaptureResourceState(Recorded,
						    Pass,
						    Handle,
						    Texture,
						    EArdaGraphStateCheckpoint::AfterTransition,
						    Transition.mStateAfter,
						    Transition.mSubresources);
					}
				}
				for (const FArdaInductorBufferTransition& Transition : Transitions.mBuffers)
				{
					FArdaInductorBuffer& Buffer = Graph.mBuffers.Get(Transition.mBuffer);
					if (!Accept(Recorded.mCommandList->BeginTrackingBufferState(*Buffer.GetResource(),
					        Transition.mStateBefore)))
					{
						return Recorded;
					}
					if (bValidateResourceStates)
					{
						CaptureResourceState(Recorded,
						    Pass,
						    Handle,
						    Buffer,
						    EArdaGraphStateCheckpoint::BeforeTransition,
						    Transition.mStateBefore);
					}

					if (IsUAVState(Transition.mStateAfter))
					{
						if (!Accept(Recorded.mCommandList->SetUAVBarriersForBuffer(*Buffer.GetResource(),
						        Transition.mbUAVBarrier)))
						{
							return Recorded;
						}
					}
					if (!Accept(Recorded.mCommandList->SetBufferState(*Buffer.GetResource(), Transition.mStateAfter)))
					{
						return Recorded;
					}
					if (bValidateResourceStates)
					{
						CaptureResourceState(Recorded,
						    Pass,
						    Handle,
						    Buffer,
						    EArdaGraphStateCheckpoint::AfterTransition,
						    Transition.mStateAfter);
					}
				}
				for (const FArdaInductorAccelerationStructureTransition& Transition : Transitions.mAccelStructs)
				{
					FArdaInductorAccelerationStructure& AccelStruct = Graph.mAccelStructs.Get(Transition.mAccelStruct);

					if (!Accept(Recorded.mCommandList->SetAccelStructState(*AccelStruct.GetResource(),
					        Transition.mStateAfter)))
					{
						return Recorded;
					}
				}
				Recorded.mCommandList->CommitBarriers();
			}

			if (!Pass.GetState().mbSentinel)
			{
				Recorded.mCommandList->BeginMarker(Pass.GetName().c_str());
				FArdaInductorPassContext Context(Program, Handle, *Recorded.mCommandList, Pass.GetState().mQueue);
				Pass.Execute(Context);
				if (Recorded.mStatus)
				{
					Recorded.mStatus = Context.GetStatus();
				}
				Recorded.mCommandList->EndMarker();
				if (!Recorded.mStatus)
				{
					return Recorded;
				}
			}
			if (bValidateResourceStates)
			{
				for (const FArdaInductorTextureTransition& State : Transitions.mTextures)
				{
					FArdaInductorTexture& Texture = Graph.mTextures.Get(State.mTexture);
					CaptureResourceState(Recorded,
					    Pass,
					    Handle,
					    Texture,
					    EArdaGraphStateCheckpoint::AfterPass,
					    State.mStateAfter,
					    State.mSubresources);
				}
				for (const FArdaInductorBufferTransition& State : Transitions.mBuffers)
				{
					FArdaInductorBuffer& Buffer = Graph.mBuffers.Get(State.mBuffer);
					CaptureResourceState(Recorded,
					    Pass,
					    Handle,
					    Buffer,
					    EArdaGraphStateCheckpoint::AfterPass,
					    State.mStateAfter);
				}
			}
			for (const auto& Transfer : Transitions.mTextureReleases)
			{
				if (!Accept(
				        RecordQueueTransfer(Graph, Recorded, Pass, Handle, Transfer, false, bValidateResourceStates)))
				{
					return Recorded;
				}
			}
			for (const auto& Transfer : Transitions.mBufferReleases)
			{
				if (!Accept(
				        RecordQueueTransfer(Graph, Recorded, Pass, Handle, Transfer, false, bValidateResourceStates)))
				{
					return Recorded;
				}
			}
			if (bClose)
			{
				const auto CloseStatus = Recorded.mCommandList->Close();
				if (Recorded.mStatus)
				{
					Recorded.mStatus = CloseStatus;
				}
			}
			return Recorded;
		}
	}

	const FArdaGraphExecutionResult& FArdaInductorCommandExecutor::Submit(FArdaInductorCommandProgram& Program,
	    const FArdaGraphExecuteOptions& Options)
	{
		auto& Graph = *Program.mImpl;
		if (!Graph.mbCompiled || !Graph.mDevice || Graph.mbFailed || !Graph.mActivePassAccess.empty() ||
		    (Graph.mbExecutionStarted && !Graph.mbExecuted))
		{
			Graph.mExecutionResult.mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Inductor submission requires a finalized, retired, successful command program.");
			return Graph.mExecutionResult;
		}
		Graph.mbExecutionStarted = true;
		Graph.mbExecuted = false;
		Graph.mSubmittingQueue = -1;
		FArdaInductorRecordingFailureGuard FailureGuard{Graph};

		Graph.mExecutionResult = {};
		Graph.mExecutionResult.mQueueDependencies = Graph.mPlan.mQueueDependencies;
		auto RuntimeTransitions = BuildPhysicalTransitions(Graph);
		for (const auto& Alias : Graph.mReusableAliasActivations)
		{
			RuntimeTransitions[Alias.mConsumer.GetIndex()].mAliasingResources.push_back(
			    {Alias.mType, Alias.mResourceIndex, Alias.mType == EArdaGraphResourceType::Texture});
			++Graph.mExecutionResult.mAliasingBarrierCount;
		}

		eastl::vector<FArdaInductorRecordedCommand> Recorded(Graph.mPasses.GetCount());
		eastl::vector<uint32_t> Levels(Graph.mPasses.GetCount(), 0);
		uint32_t MaxLevel = 0;
		for (FArdaInductorCommandHandle Handle : Graph.mPlan.mExecutionOrder)
		{
			const FArdaInductorCommand& Pass = Graph.mPasses.Get(Handle);
			if (Handle == Graph.mPlan.mPrologue)
			{
				continue;
			}
			uint32_t Level = 0;

			// Fold both data producers and explicit synchronization producers
			// into the earliest safe CPU recording wave for this pass.
			auto AccumulateLevel = [&Graph, &Levels, &Level](FArdaInductorCommandHandle Producer)
			{
				const FArdaInductorCommand* ProducerPass = Graph.mPasses.TryGet(Producer);
				if (ProducerPass != nullptr)
				{
					Level = eastl::max(Level, Levels[Producer.GetIndex()] + 1u);
				}
			};
			for (FArdaInductorCommandHandle Producer : Pass.GetState().mProducers)
			{
				AccumulateLevel(Producer);
			}

			Levels[Handle.GetIndex()] = Level;
			MaxLevel = eastl::max(MaxLevel, Level);
		}

		const auto RecordBatch = [&](FArdaInductorCommandHandle Head)
		{
			Recorded[Head.GetIndex()] =
			    RecordPass(Program, Graph, Head, RuntimeTransitions[Head.GetIndex()], Options.mbValidateResourceStates);
		};

		const uint32_t HardwareThreads = eastl::max(1u, std::thread::hardware_concurrency());
		const uint32_t MaxWorkers =
		    Options.mMaxRecordingThreads == 0 ? HardwareThreads : eastl::max(1u, Options.mMaxRecordingThreads);
		for (uint32_t Level = 0; Level <= MaxLevel; ++Level)
		{
			eastl::vector<FArdaInductorCommandHandle> ParallelPasses;
			eastl::vector<FArdaInductorCommandHandle> SerialPasses;
			for (FArdaInductorCommandHandle Handle : Graph.mPlan.mExecutionOrder)
			{
				if (Levels[Handle.GetIndex()] != Level)
				{
					continue;
				}
				const FArdaInductorCommand& Pass = Graph.mPasses.Get(Handle);
				const bool bHasWork = !Pass.GetState().mbSentinel ||
				    !RuntimeTransitions[Handle.GetIndex()].mTextures.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mBuffers.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mAccelStructs.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mTextureAcquires.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mBufferAcquires.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mTextureReleases.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mBufferReleases.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mAliasingResources.empty();
				if (Pass.mbRecordAtSubmit)
				{
					continue;
				}
				if (!bHasWork)
				{
					continue;
				}
				if (Options.mbParallelRecording && !Pass.mbSerialRecord)
				{
					ParallelPasses.push_back(Handle);
				}
				else
				{
					SerialPasses.push_back(Handle);
				}
			}

			for (size_t Begin = 0; Begin < ParallelPasses.size(); Begin += MaxWorkers)
			{
				const size_t End = eastl::min(ParallelPasses.size(), Begin + MaxWorkers);
				eastl::vector<std::future<void>> Futures;
				Futures.reserve(End - Begin);
				Graph.mExecutionResult.mbUsedParallelRecording |= End - Begin > 1;
				for (size_t Index = Begin; Index < End; ++Index)
				{
					const FArdaInductorCommandHandle Handle = ParallelPasses[Index];
					Futures.push_back(std::async(std::launch::async,

					    // Each job owns a distinct command list and writes a
					    // distinct pass-indexed result slot; shared graph data
					    // is read-only during recording except guarded access
					    // validation managed by execution contexts.
					    [&RecordBatch, Handle]
					    {
						    RecordBatch(Handle);
					    }));
				}
				for (auto& Future : Futures)
				{
					Future.get();
				}
			}
			for (FArdaInductorCommandHandle Handle : SerialPasses)
			{
				RecordBatch(Handle);
			}
		}

		// Ordinary recording completes before submission. RecordAtSubmit passes
		// can fail later; report every accepted submission so the frame can retire safely.
		for (const auto& Pass : Recorded)
		{
			if (!Pass.mStatus)
			{
				Graph.mExecutionResult.mStatus = Pass.mStatus;
				return Graph.mExecutionResult;
			}
		}
		eastl::vector<uint64_t> PassInstances(Graph.mPasses.GetCount(), 0);

		// Submission order remains deterministic even when recording completed
		// out of order. Per-pass instances become synchronization tokens for
		// later cross-queue consumers.
		for (FArdaInductorCommandHandle Handle : Graph.mPlan.mExecutionOrder)
		{
			FArdaInductorRecordedCommand& Pass = Recorded[Handle.GetIndex()];
			if (Graph.mPasses.Get(Handle).mbRecordAtSubmit)
			{
				RecordBatch(Handle);
				if (!Pass.mStatus)
				{
					Graph.mExecutionResult.mStatus = Pass.mStatus;
					break;
				}
			}
			if (!Pass.mCommandList)
			{
				continue;
			}

			for (FArdaGraphStateConformanceRecord& Record : Pass.mStateConformanceRecords)
			{
				if (!Record.IsConsistent())
				{
					++Graph.mExecutionResult.mStateConformanceFailureCount;
				}
				Graph.mExecutionResult.mStateConformanceRecords.push_back(eastl::move(Record));
			}
			if (Options.mbValidateResourceStates && Graph.mExecutionResult.mStateConformanceFailureCount != 0)
			{
				Graph.mExecutionResult.mStatus = arda::FArdaRHIStatus::Error(arda::EArdaRHIResult::InvalidState,
				    "Inductor resource state differs from the facade/backend/native state.");
				break;
			}

			const size_t ConsumerQueueIndex = arda::GetArdaRHIQueueIndex(Pass.mQueue);
			eastl::array<uint64_t, arda::ArdaRHIQueueTypeCount> RequiredInstances{};

			for (const FArdaGraphQueueDependency& Dependency : Graph.mExecutionResult.mQueueDependencies)
			{
				if (Dependency.mConsumer != Handle.GetIndex())
				{
					continue;
				}
				const uint64_t ProducerInstance = PassInstances[Dependency.mProducer];
				if (ProducerInstance == 0)
				{
					continue;
				}
				auto& Required = RequiredInstances[arda::GetArdaRHIQueueIndex(Dependency.mProducerQueue)];
				Required = eastl::max(Required, ProducerInstance);
			}

			for (size_t QueueIndex = 0; QueueIndex < RequiredInstances.size(); ++QueueIndex)
			{
				if (!RequiredInstances[QueueIndex])
				{
					continue;
				}
				auto Status = Graph.mDevice->QueueWait(Pass.mQueue,
				    static_cast<arda::EArdaRHIQueueType>(QueueIndex),
				    RequiredInstances[QueueIndex]);
				if (!Status)
				{
					Graph.mExecutionResult.mStatus = eastl::move(Status);
					break;
				}
				++Graph.mExecutionResult.mQueueWaitCount;
			}
			if (!Graph.mExecutionResult.mStatus)
			{
				break;
			}

			Graph.mSubmittingQueue = static_cast<int32_t>(ConsumerQueueIndex);
			const auto SubmitResult = Graph.mDevice->ExecuteCommandList(Pass.mCommandList);
			Graph.mSubmittingQueue = -1;
			if (!SubmitResult)
			{
				Graph.mExecutionResult.mStatus = SubmitResult.mStatus;
				++Graph.mExecutionResult.mSubmissionFailureCount;
				if (SubmitResult.mStatus.mCode == arda::EArdaRHIResult::InvalidState)
				{
					++Graph.mExecutionResult.mStateConformanceFailureCount;
				}
				break;
			}
			const uint64_t Instance = SubmitResult.mValue;
			PassInstances[Handle.GetIndex()] = Instance;
			Graph.mExecutionResult.mLastSubmittedInstances[ConsumerQueueIndex] = Instance;
			++Graph.mExecutionResult.mSubmittedCommandListCount;
		}

		Graph.mDevice->RunGarbageCollection();
		Graph.mbExecuted = Graph.mExecutionResult.mStatus.IsSuccess();
		FailureGuard.mbCompleted = Graph.mbExecuted;
		ARDA_TRACE_COUNTER("Inductor Submitted Command Lists", Graph.mExecutionResult.mSubmittedCommandListCount);
		ARDA_TRACE_COUNTER("Inductor Queue Waits", Graph.mExecutionResult.mQueueWaitCount);
		return Graph.mExecutionResult;
	}
}
