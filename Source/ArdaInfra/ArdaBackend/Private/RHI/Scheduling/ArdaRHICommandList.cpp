#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	FArdaCommandList::FArdaCommandList(FArdaRHIDeviceImpl* Device,
	    EArdaRHIQueueType Queue,
	    eastl::unique_ptr<IArdaProviderCommandList> Native,
	    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
	    : FArdaResource(EArdaRHIResourceType::CommandList, "CommandList", Device, eastl::move(LifetimeTracker)),
	      mDevice(Device),
	      mQueue(Queue),
	      mNative(eastl::move(Native))
	{
	}

	IArdaRHIDevice* FArdaCommandList::GetDevice() const noexcept
	{
		return mDevice.Get();
	}

	bool FArdaCommandList::RetainOwned(const FArdaResource* Resource) const
	{
		if (!Resource || Resource->GetOwner() != GetOwner())
		{
			return false;
		}
		mRetainedResources.try_emplace(Resource, const_cast<FArdaResource*>(Resource));
		return true;
	}

	void FArdaCommandList::ClearRecordingState()
	{
		mRetainedResources.clear();
		mEmptyBindingSets.clear();
		mMeshletState = {};
		mGraphicsState = {};
		mPipelineKind = EArdaPipelineKind::None;
		mbRecordingOpen = true;
		mPushConstantCapacity = 0;
		mRecordingStatus = {};
		mCopyCompletions.clear();
		mFacadeTextureStates.clear();
		mTouchedTextureStates.clear();
		mFacadeBufferStates.clear();
		mFacadeSamplerFeedbackStates.clear();
		mFacadeTextureQueueOwners.clear();
		mFacadeBufferQueueOwners.clear();
		mFacadeAccelStructStates.clear();
		mFacadeOpacityMicromapStates.clear();
		mExpectedTextureStartStates.clear();
		mExpectedBufferStartStates.clear();
	}

	FArdaRHIStatus FArdaCommandList::Open()
	{
		const auto Status = mNative->Open();
		if (Status)
		{
			ClearRecordingState();
		}
		return Status;
	}

	eastl::vector<EArdaRHIResourceState>& FArdaCommandList::GetFacadeTextureStates(FArdaTexture& Texture) const
	{
		auto Existing = mFacadeTextureStates.find(&Texture);
		if (Existing != mFacadeTextureStates.end())
		{
			return Existing->second;
		}
		eastl::vector<EArdaRHIResourceState> States;
		{
			std::lock_guard<std::mutex> Lock(Texture.mFacadeStateMutex);
			States = Texture.mFacadeStates;
		}
		if (States.empty())
		{
			States.assign(static_cast<size_t>(Texture.mDesc.mMipLevels) * Texture.mDesc.mArraySize *
			        GetArdaRHIFormatPlaneCount(Texture.mDesc.mFormat),
			    Texture.mDesc.mInitialState);
		}
		return mFacadeTextureStates.emplace(&Texture, eastl::move(States)).first->second;
	}

	void FArdaCommandList::StoreTextureState(FArdaTexture& Texture,
	    const FArdaRHITextureSubresourceRange& Range,
	    EArdaRHIResourceState State)
	{
		auto& States = GetFacadeTextureStates(Texture);
		auto& Touched = mTouchedTextureStates[&Texture];
		if (Touched.empty())
		{
			Touched.assign(States.size(), 0);
		}
		StoreFacadeTextureState(States, Texture.mDesc, Range, State);
		for (const auto [MipLevel, ArraySlice, Plane] : FArdaTextureSubresources(Range.Resolve(Texture.mDesc)))
		{
			Touched[TextureStateIndex(Texture.mDesc, MipLevel, ArraySlice, Plane)] = 1;
		}
	}

	void FArdaCommandList::CommitFacadeStates()
	{
		for (const auto& Entry : mTouchedTextureStates)
		{
			std::lock_guard<std::mutex> Lock(Entry.first->mFacadeStateMutex);
			auto& Submitted = Entry.first->mFacadeStates;
			if (Submitted.empty())
			{
				Submitted.assign(Entry.second.size(), Entry.first->mDesc.mInitialState);
			}
			// Lists can record independently against the same texture. Only publish ranges this list touched.
			for (size_t Index = 0; Index < Entry.second.size(); ++Index)
			{
				if (Entry.second[Index])
				{
					Submitted[Index] = mFacadeTextureStates.at(Entry.first)[Index];
				}
			}
		}
		for (const auto& Entry : mFacadeTextureQueueOwners)
		{
			std::lock_guard<std::mutex> Lock(Entry.first->mFacadeStateMutex);
			Entry.first->mFacadeQueueOwner = Entry.second;
			Entry.first->mbFacadeQueueOwnerKnown = true;
		}
		for (const auto& Entry : mFacadeBufferStates)
		{
			std::lock_guard<std::mutex> Lock(Entry.first->mFacadeStateMutex);
			Entry.first->mFacadeState = Entry.second;
			Entry.first->mbFacadeStateKnown = true;
		}
		for (const auto& Entry : mFacadeBufferQueueOwners)
		{
			std::lock_guard<std::mutex> Lock(Entry.first->mFacadeStateMutex);
			Entry.first->mFacadeQueueOwner = Entry.second;
			Entry.first->mbFacadeQueueOwnerKnown = true;
		}
		for (const auto& Entry : mFacadeAccelStructStates)
		{
			std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
			Entry.first->mFacadeState = Entry.second.mState;
			if (Entry.second.mbLifecycleWritten)
			{
				Entry.first->mBuildState = Entry.second.mBuildState;
			}
		}
		for (const auto& Entry : mFacadeOpacityMicromapStates)
		{
			std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
			Entry.first->mFacadeState = Entry.second.mState;
			if (Entry.second.mbLifecycleWritten)
			{
				Entry.first->mBuildState = Entry.second.mBuildState;
			}
		}
		for (const auto& Entry : mFacadeSamplerFeedbackStates)
		{
			std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
			Entry.first->mFacadeState = Entry.second;
			Entry.first->mbFacadeStateKnown = true;
		}
	}

	FArdaRHIStatus FArdaCommandList::ValidateFacadeStartStates() const
	{
		if (!mRecordingStatus)
		{
			return mRecordingStatus;
		}
		for (const auto& Entry : mExpectedTextureStartStates)
		{
			FArdaTexture* Texture = Entry.first;
			eastl::vector<EArdaRHIResourceState> Submitted;
			{
				std::lock_guard<std::mutex> Lock(Texture->mFacadeStateMutex);
				Submitted = Texture->mFacadeStates;
			}
			if (Submitted.empty())
			{
				Submitted.assign(static_cast<size_t>(Texture->mDesc.mMipLevels) * Texture->mDesc.mArraySize *
				        GetArdaRHIFormatPlaneCount(Texture->mDesc.mFormat),
				    Texture->mDesc.mInitialState);
			}
			for (size_t Index = 0; Index < Entry.second.size(); ++Index)
			{
				if (Entry.second[Index] != EArdaRHIResourceState::Unknown &&
				    Entry.second[Index] != Submitted[Index])
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
					    "Facade texture start state differs at submission.");
				}
			}
		}
		for (const auto& Entry : mExpectedBufferStartStates)
		{
			EArdaRHIResourceState Submitted = Entry.first->mDesc.mInitialState;
			{
				std::lock_guard<std::mutex> Lock(Entry.first->mFacadeStateMutex);
				if (Entry.first->mbFacadeStateKnown)
				{
					Submitted = Entry.first->mFacadeState;
				}
			}
			if (Submitted != Entry.second)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "Facade buffer start state differs at submission.");
			}
		}
		return {};
	}

	FArdaRHIStatus FArdaCommandList::Reset()
	{
		const auto Status = mNative->Reset();
		if (Status)
		{
			ClearRecordingState();
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::BeginTimerQuery(IArdaRHITimerQuery& Query)
	{
		auto* Native = Cast<FArdaTimerQuery>(&Query);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		return mNative->BeginTimerQuery(Native->mNative);
	}

	FArdaRHIStatus FArdaCommandList::EndTimerQuery(IArdaRHITimerQuery& Query)
	{
		auto* Native = Cast<FArdaTimerQuery>(&Query);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		return mNative->EndTimerQuery(Native->mNative);
	}
}
