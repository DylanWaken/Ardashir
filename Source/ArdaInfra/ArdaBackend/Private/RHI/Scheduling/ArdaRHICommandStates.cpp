#include "RHI/Scheduling/ArdaRHICommandListImpl.h"
#include "RHI/Scheduling/ArdaRHITextureStates.h"
#include <mutex>
#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	FArdaRHIStatus FArdaCommandList::SetTextureState(IArdaRHITexture& Texture,
	    const FArdaRHITextureSubresourceRange& Range,
	    EArdaRHIResourceState State)
	{
		auto* Native = Cast<FArdaTexture>(&Texture);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		const FArdaRHIStatus Status = mNative->SetTextureState(Native->mNative, Native->mDesc, Range, State);
		if (Status)
		{
			StoreTextureState(*Native, Range, State);
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::SetBufferState(IArdaRHIBuffer& Buffer, EArdaRHIResourceState State)
	{
		auto* Native = Cast<FArdaBuffer>(&Buffer);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		const FArdaRHIStatus Status = mNative->SetBufferState(Native->mNative, Native->mDesc, State);
		if (Status)
		{
			mFacadeBufferStates[Native] = State;
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::TransitionTexture(IArdaRHITexture& Texture,
	    const FArdaRHITextureTransitionDesc& Transition)
	{
		auto* Native = Cast<FArdaTexture>(&Texture);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		const auto Current =
		    LoadFacadeTextureState(GetFacadeTextureStates(*Native), Native->mDesc, Transition.mSubresources);
		if (!Current)
		{
			return Current.mStatus;
		}
		if (!HasAnyFlags(Transition.mFlags, EArdaRHITransitionFlags::Discard) &&
		    Transition.mStateBefore != EArdaRHIResourceState::Unknown && Current.mValue != Transition.mStateBefore)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Texture transition before-state differs from the facade state.");
		}
		const FArdaRHIStatus Status = mNative->TransitionTexture(Native->mNative, Native->mDesc, Transition);
		if (Status && Transition.mbQueueOwnershipTransfer)
		{
			mFacadeTextureQueueOwners[Native] = Transition.mDestinationQueue;
		}
		if (Status && !HasAnyFlags(Transition.mFlags, EArdaRHITransitionFlags::BeginOnly))
		{
			StoreTextureState(*Native, Transition.mSubresources, Transition.mStateAfter);
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::TransitionBuffer(IArdaRHIBuffer& Buffer,
	    const FArdaRHIBufferTransitionDesc& Transition)
	{
		auto* Native = Cast<FArdaBuffer>(&Buffer);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		auto Existing = mFacadeBufferStates.find(Native);
		EArdaRHIResourceState Current = Native->mDesc.mInitialState;
		if (Existing != mFacadeBufferStates.end())
		{
			Current = Existing->second;
		}
		else
		{
			std::lock_guard<std::mutex> Lock(Native->mFacadeStateMutex);
			if (Native->mbFacadeStateKnown)
			{
				Current = Native->mFacadeState;
			}
		}
		if (!HasAnyFlags(Transition.mFlags, EArdaRHITransitionFlags::Discard) &&
		    Transition.mStateBefore != EArdaRHIResourceState::Unknown && Current != Transition.mStateBefore)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Buffer transition before-state differs from the facade state.");
		}
		const FArdaRHIStatus Status = mNative->TransitionBuffer(Native->mNative, Native->mDesc, Transition);
		if (Status && Transition.mbQueueOwnershipTransfer)
		{
			mFacadeBufferQueueOwners[Native] = Transition.mDestinationQueue;
		}
		if (Status && !HasAnyFlags(Transition.mFlags, EArdaRHITransitionFlags::BeginOnly))
		{
			mFacadeBufferStates[Native] = Transition.mStateAfter;
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::SetAccelStructState(IArdaRHIAccelStruct& Resource, EArdaRHIResourceState State)
	{
		auto* AccelStruct = Cast<FArdaAccelStruct>(&Resource);
		if (!AccelStruct || !RetainOwned(AccelStruct))
		{
			return WrongDevice();
		}
		if (!HasAnyFlags(State, EArdaRHIResourceState::AccelStructRead) &&
		    !HasAnyFlags(State, EArdaRHIResourceState::AccelStructWrite))
		{
			return Invalid("Acceleration structures require a read or write state.");
		}
		const FArdaRHIStatus Status = mNative->SetAccelStructState(AccelStruct->mNative, State);
		if (Status)
		{
			auto Existing = mFacadeAccelStructStates.find(AccelStruct);
			if (Existing == mFacadeAccelStructStates.end())
			{
				std::lock_guard<std::mutex> Lock(AccelStruct->mStateMutex);
				Existing = mFacadeAccelStructStates
				               .emplace(AccelStruct, FArdaAccelStructTracking{State, AccelStruct->mBuildState})
				               .first;
			}
			else
			{
				Existing->second.mState = State;
			}
		}
		return Status;
	}

	TArdaRHIResult<FArdaRHIResourceStateSnapshot> FArdaCommandList::QueryAccelStructState(
	    IArdaRHIAccelStruct& Resource) const
	{
		auto* AccelStruct = Cast<FArdaAccelStruct>(&Resource);
		if (!AccelStruct || !RetainOwned(AccelStruct))
		{
			return {{}, WrongDevice()};
		}
		FArdaAccelStructTracking Tracking;
		const auto Existing = mFacadeAccelStructStates.find(AccelStruct);
		if (Existing != mFacadeAccelStructStates.end())
		{
			Tracking = Existing->second;
		}
		else
		{
			std::lock_guard<std::mutex> Lock(AccelStruct->mStateMutex);
			Tracking.mState = AccelStruct->mFacadeState;
			Tracking.mBuildState = AccelStruct->mBuildState;
		}
		auto Native = mNative->QueryAccelStructState(AccelStruct->mNative);
		if (!Native)
		{
			return {{}, eastl::move(Native.mStatus)};
		}
		FArdaRHIResourceStateSnapshot Snapshot;
		Snapshot.mFacadeState = Tracking.mState;
		Snapshot.mQueue = mQueue;
		Snapshot.mNative = eastl::move(Native.mValue);
		Snapshot.mbFacadeKnown = true;
		return {eastl::move(Snapshot), {}};
	}

	FArdaRHIStatus FArdaCommandList::BeginTrackingTextureState(IArdaRHITexture& Texture,
	    const FArdaRHITextureSubresourceRange& Range,
	    EArdaRHIResourceState State)
	{
		auto* Native = Cast<FArdaTexture>(&Texture);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		const FArdaRHIStatus Status =
		    mNative->BeginTrackingTextureState(Native->mNative, Native->mDesc, Range, State);
		if (Status)
		{
			auto& Expected = mExpectedTextureStartStates[Native];
			if (Expected.empty())
			{
				Expected.assign(static_cast<size_t>(Native->mDesc.mMipLevels) * Native->mDesc.mArraySize *
				        GetArdaRHIFormatPlaneCount(Native->mDesc.mFormat),
				    EArdaRHIResourceState::Unknown);
			}
			StoreFacadeTextureState(Expected, Native->mDesc, Range, State);
			StoreTextureState(*Native, Range, State);
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::BeginTrackingBufferState(IArdaRHIBuffer& Buffer, EArdaRHIResourceState State)
	{
		auto* Native = Cast<FArdaBuffer>(&Buffer);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		const FArdaRHIStatus Status = mNative->BeginTrackingBufferState(Native->mNative, Native->mDesc, State);
		if (Status)
		{
			mExpectedBufferStartStates[Native] = State;
			mFacadeBufferStates[Native] = State;
		}
		return Status;
	}

	TArdaRHIResult<FArdaRHIResourceStateSnapshot> FArdaCommandList::QueryTextureState(IArdaRHITexture& Texture,
	    const FArdaRHITextureSubresourceRange& Range) const
	{
		auto* Native = Cast<FArdaTexture>(&Texture);
		if (!Native || !RetainOwned(Native))
		{
			return {{}, WrongDevice()};
		}
		auto Facade = LoadFacadeTextureState(GetFacadeTextureStates(*Native), Native->mDesc, Range);
		if (!Facade)
		{
			return {{}, eastl::move(Facade.mStatus)};
		}
		auto Backend = mNative->QueryTextureState(Native->mNative, Native->mDesc, Range);
		if (!Backend)
		{
			return {{}, eastl::move(Backend.mStatus)};
		}
		FArdaRHIResourceStateSnapshot Snapshot;
		Snapshot.mFacadeState = Facade.mValue;
		Snapshot.mQueue = mQueue;
		auto QueueOwner = mFacadeTextureQueueOwners.find(Native);
		if (QueueOwner != mFacadeTextureQueueOwners.end())
		{
			Snapshot.mFacadeQueueOwner = QueueOwner->second;
			Snapshot.mbFacadeQueueOwnerKnown = true;
		}
		else
		{
			std::lock_guard<std::mutex> Lock(Native->mFacadeStateMutex);
			Snapshot.mFacadeQueueOwner = Native->mFacadeQueueOwner;
			Snapshot.mbFacadeQueueOwnerKnown = Native->mbFacadeQueueOwnerKnown;
		}
		Snapshot.mNative = eastl::move(Backend.mValue);
		Snapshot.mbFacadeKnown = true;
		return {eastl::move(Snapshot), {}};
	}

	TArdaRHIResult<FArdaRHIResourceStateSnapshot> FArdaCommandList::QueryBufferState(IArdaRHIBuffer& Buffer) const
	{
		auto* Native = Cast<FArdaBuffer>(&Buffer);
		if (!Native || !RetainOwned(Native))
		{
			return {{}, WrongDevice()};
		}
		FArdaRHIResourceStateSnapshot Snapshot;
		Snapshot.mQueue = mQueue;
		auto Existing = mFacadeBufferStates.find(Native);
		if (Existing == mFacadeBufferStates.end())
		{
			EArdaRHIResourceState State = Native->mDesc.mInitialState;
			{
				std::lock_guard<std::mutex> Lock(Native->mFacadeStateMutex);
				if (Native->mbFacadeStateKnown)
				{
					State = Native->mFacadeState;
				}
			}
			Existing = mFacadeBufferStates.emplace(Native, State).first;
		}
		Snapshot.mFacadeState = Existing->second;
		Snapshot.mbFacadeKnown = true;
		auto QueueOwner = mFacadeBufferQueueOwners.find(Native);
		if (QueueOwner != mFacadeBufferQueueOwners.end())
		{
			Snapshot.mFacadeQueueOwner = QueueOwner->second;
			Snapshot.mbFacadeQueueOwnerKnown = true;
		}
		else
		{
			std::lock_guard<std::mutex> Lock(Native->mFacadeStateMutex);
			Snapshot.mFacadeQueueOwner = Native->mFacadeQueueOwner;
			Snapshot.mbFacadeQueueOwnerKnown = Native->mbFacadeQueueOwnerKnown;
		}
		auto Backend = mNative->QueryBufferState(Native->mNative, Native->mDesc);
		if (!Backend)
		{
			return {{}, eastl::move(Backend.mStatus)};
		}
		Snapshot.mNative = eastl::move(Backend.mValue);
		return {eastl::move(Snapshot), {}};
	}

	TArdaRHIResult<FArdaRHIResourceStateSnapshot> FArdaCommandList::QuerySamplerFeedbackTextureState(
	    IArdaRHISamplerFeedbackTexture& Texture) const
	{
		auto* Native = Cast<FArdaSamplerFeedbackTexture>(&Texture);
		if (!Native || !RetainOwned(Native))
		{
			return {{}, WrongDevice()};
		}
		auto Existing = mFacadeSamplerFeedbackStates.find(Native);
		if (Existing == mFacadeSamplerFeedbackStates.end())
		{
			EArdaRHIResourceState State = Native->mDesc.mInitialState;
			{
				std::lock_guard<std::mutex> Lock(Native->mStateMutex);
				if (Native->mbFacadeStateKnown)
				{
					State = Native->mFacadeState;
				}
			}
			Existing = mFacadeSamplerFeedbackStates.emplace(Native, State).first;
		}
		auto Backend = mNative->QuerySamplerFeedbackTextureState(Native->mNative);
		if (!Backend)
		{
			return {{}, eastl::move(Backend.mStatus)};
		}
		FArdaRHIResourceStateSnapshot Snapshot;
		Snapshot.mFacadeState = Existing->second;
		Snapshot.mQueue = mQueue;
		Snapshot.mNative = eastl::move(Backend.mValue);
		Snapshot.mbFacadeKnown = true;
		return {eastl::move(Snapshot), {}};
	}

	FArdaRHIStatus FArdaCommandList::SetUAVBarriersForTexture(IArdaRHITexture& Texture, bool bEnabled)
	{
		auto* Native = Cast<FArdaTexture>(&Texture);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		return mNative->SetUAVBarriersForTexture(Native->mNative, bEnabled);
	}

	FArdaRHIStatus FArdaCommandList::SetUAVBarriersForBuffer(IArdaRHIBuffer& Buffer, bool bEnabled)
	{
		auto* Native = Cast<FArdaBuffer>(&Buffer);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		return mNative->SetUAVBarriersForBuffer(Native->mNative, bEnabled);
	}

	FArdaRHIStatus FArdaCommandList::AliasingBarrier(IArdaRHIResource* ResourceBefore,
	    IArdaRHIResource* ResourceAfter)
	{
		auto* Before = Cast<FArdaResource>(ResourceBefore);
		auto* After = Cast<FArdaResource>(ResourceAfter);
		if ((Before && !RetainOwned(Before)) || (After && !RetainOwned(After)))
		{
			return WrongDevice();
		}
		if (!Before && !After)
		{
			return Invalid("An aliasing barrier requires at least one resource.");
		}
		return mNative->AliasingBarrier(GetNativeObject(ResourceBefore), GetNativeObject(ResourceAfter));
	}
}
