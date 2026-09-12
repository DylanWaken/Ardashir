#include "Compute/ArdaCudaSequence.h"

namespace arda
{
	FArdaCudaSequence::FArdaCudaSequence(FArdaRHIDeviceRef Device,
	    EArdaRHIQueueType Queue,
	    eastl::shared_ptr<FArdaCudaGraphCache> GraphCache)
	    : mDevice(eastl::move(Device)),
	      mQueue(Queue),
	      mGraphCache(GraphCache ? eastl::move(GraphCache) : eastl::make_shared<FArdaCudaGraphCache>())
	{
		if (!mDevice)
		{
			mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA sequence requires a device.");
			return;
		}
		const auto Capabilities = mDevice->GetCudaCapabilities();
		if (!Capabilities)
		{
			mStatus = FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Capabilities.mUnavailableReason.c_str());
		}
		else if ((Queue != EArdaRHIQueueType::Graphics && Queue != EArdaRHIQueueType::Compute) ||
		    (Capabilities.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG && Queue != EArdaRHIQueueType::Graphics))
		{
			mStatus = FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "CUDA sequence does not support this queue.");
		}
	}

	FArdaRHIStatus FArdaCudaSequence::AddPlan(const eastl::shared_ptr<const FArdaCudaDispatchPlan>& Plan)
	{
		if (!mStatus)
		{
			return mStatus;
		}
		if (!Plan || Plan->mDevice != mDevice)
		{
			return mStatus = FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
			           "CUDA sequence step belongs to another device.");
		}
		if (Plan->mQueue != mQueue)
		{
			return mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			           "CUDA sequence step belongs to another queue.");
		}
		if (Plan->mSelection.mbNoWork)
		{
			if (!Plan->mDispatch.mKernels.empty() || !Plan->mDispatch.mBindings.empty())
			{
				return mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				           "NoWork sequence step must be empty.");
			}
			return {};
		}
		mStatus = ValidateArdaCudaKernels(Plan->mDispatch.mKernels,
		    Plan->mDispatch.mBindings.size(),
		    mDevice->GetCudaCapabilities());
		if (mStatus)
		{
			mDispatches.push_back(Plan->mDispatch);
		}
		return mStatus;
	}

	const FArdaRHIStatus& FArdaCudaSequence::GetStatus() const noexcept
	{
		return mStatus;
	}

	FArdaRHIStatus FArdaCudaSequence::BeginTimingRegion(eastl::shared_ptr<FArdaCudaTimingQuery> Query,
	    uint64_t RegionId)
	{
		if (!mStatus)
		{
			return mStatus;
		}
		if (!Query || mbTimingRegionOpen || (mTimingQuery && Query != mTimingQuery))
		{
			return mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			           "CUDA timing regions require one query and cannot nest.");
		}
		for (const auto& Region : mTimingRegions)
		{
			if (Region.mRegionId == RegionId)
			{
				return mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				           "CUDA timing region IDs must be unique within a sequence.");
			}
		}
		mTimingQuery = eastl::move(Query);
		const auto Operation = static_cast<uint32_t>(mDispatches.size());
		mTimingRegions.push_back({RegionId, Operation, Operation});
		mbTimingRegionOpen = true;
		return {};
	}

	FArdaRHIStatus FArdaCudaSequence::EndTimingRegion()
	{
		if (!mStatus)
		{
			return mStatus;
		}
		if (!mbTimingRegionOpen)
		{
			return mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			           "CUDA timing region end requires a matching begin.");
		}
		mTimingRegions.back().mEndOperation = static_cast<uint32_t>(mDispatches.size());
		mbTimingRegionOpen = false;
		return {};
	}

	const eastl::shared_ptr<FArdaCudaGraphCache>& FArdaCudaSequence::GetGraphCache() const noexcept
	{
		return mGraphCache;
	}

	size_t FArdaCudaSequence::GetKernelCount() const noexcept
	{
		size_t Count = 0;
		for (const auto& Dispatch : mDispatches)
		{
			Count += Dispatch.mKernels.front().mEntry ? 1 : 0;
		}
		return Count;
	}

	size_t FArdaCudaSequence::GetOperationCount() const noexcept
	{
		return mDispatches.size();
	}

	FArdaRHIStatus FArdaCudaSequence::DispatchDeferred(IArdaRHICommandList& Commands) const
	{
		if (!mStatus)
		{
			return mStatus;
		}
		if (mbTimingRegionOpen)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "CUDA timing regions must be closed before dispatch.");
		}
		if (Commands.GetDevice() != mDevice.Get())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice, "CUDA sequence belongs to another device.");
		}
		if (Commands.GetQueueType() != mQueue)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA sequence belongs to another queue.");
		}
		auto Batch = eastl::make_shared<FArdaCudaGraphBatch>();
		Batch->mCache = mGraphCache;
		for (const auto& Region : mTimingRegions)
		{
			if (Region.mFirstOperation != Region.mEndOperation)
			{
				Batch->mTimingRegions.push_back(Region);
			}
		}
		if (!Batch->mTimingRegions.empty())
		{
			Batch->mTimingQuery = mTimingQuery;
		}
		auto Dispatches = mDispatches;
		for (auto& Dispatch : Dispatches)
		{
			for (const auto& Binding : Dispatch.mBindings)
			{
				Batch->mResources.push_back(Binding.mResource);
			}
			Dispatch.mKernels.front().mGraphBatch = Batch;
		}
		return Commands.DispatchCudaSequence(Dispatches);
	}

	TArdaRHIResult<FArdaCudaSequenceSubmission> FArdaCudaSequence::Dispatch() const
	{
		if (!mStatus)
		{
			return {{}, mStatus};
		}
		if (mbTimingRegionOpen)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			        "CUDA timing regions must be closed before dispatch.")};
		}
		FArdaCudaSequenceSubmission Result{mDevice, mQueue, 0, GetKernelCount(), GetOperationCount()};
		if (mDispatches.empty())
		{
			return {Result, {}};
		}
		auto Commands = mDevice->CreateCommandList(mQueue);
		if (!Commands)
		{
			return {{}, Commands.mStatus};
		}
		if (auto Status = Commands.mValue->Open(); !Status)
		{
			return {{}, Status};
		}
		if (auto Status = DispatchDeferred(*Commands.mValue); !Status)
		{
			return {{}, Status};
		}
		if (auto Status = Commands.mValue->Close(); !Status)
		{
			return {{}, Status};
		}
		auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
		if (!Submitted)
		{
			return {{}, Submitted.mStatus};
		}
		Result.mInstance = Submitted.mValue;
		return {Result, {}};
	}
}
