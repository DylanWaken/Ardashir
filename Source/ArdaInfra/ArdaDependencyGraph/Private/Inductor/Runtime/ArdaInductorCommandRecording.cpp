#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"
#include "Inductor/Runtime/ArdaInductorCommandConformance.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace arda
{
	[[nodiscard]] FArdaInductorRecordedCommand RecordPass(FArdaInductorCommandProgram& Program,
	    FArdaInductorCommandProgram::FArdaImpl& Graph,
	    FArdaInductorCommandHandle Handle,
	    const FArdaInductorRuntimeTransitions& Transitions,
	    bool bValidateResourceStates,
	    FArdaInductorRecordedCommand Recorded,
	    bool bClose)
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
				if (!Accept(
				        RecordQueueTransfer(Graph, Recorded, Pass, Handle, Transfer, true, bValidateResourceStates)))
				{
					return Recorded;
				}
			}
			for (const auto& Transfer : Transitions.mBufferAcquires)
			{
				if (!Accept(
				        RecordQueueTransfer(Graph, Recorded, Pass, Handle, Transfer, true, bValidateResourceStates)))
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

				if (!Accept(
				        Recorded.mCommandList->SetAccelStructState(*AccelStruct.GetResource(), Transition.mStateAfter)))
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
			if (!Accept(RecordQueueTransfer(Graph, Recorded, Pass, Handle, Transfer, false, bValidateResourceStates)))
			{
				return Recorded;
			}
		}
		for (const auto& Transfer : Transitions.mBufferReleases)
		{
			if (!Accept(RecordQueueTransfer(Graph, Recorded, Pass, Handle, Transfer, false, bValidateResourceStates)))
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
