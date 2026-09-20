#pragma once

#include "Inductor/Runtime/ArdaInductorCommandRecording.h"

#include <EASTL/type_traits.h>

namespace arda
{
	[[nodiscard]] inline arda::EArdaRHIPipeline GetTransitionPipeline(arda::EArdaRHIQueueType Queue) noexcept
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
}
