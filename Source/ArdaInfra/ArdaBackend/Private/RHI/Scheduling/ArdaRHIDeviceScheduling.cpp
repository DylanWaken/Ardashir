#include <cstring>
#include <new>
#include "RHI/Scheduling/ArdaRHICommandListImpl.h"
#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	TArdaRHIResult<FArdaRHIEventQueryRef> FArdaRHIDeviceImpl::CreateEventQuery()
	{
		auto Native = mDevice->CreateEventQuery();
		if (!Native)
		{
			return Failure<FArdaRHIEventQueryRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIEventQueryRef(
		            new FArdaEventQuery("EventQuery", eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHITimerQueryRef> FArdaRHIDeviceImpl::CreateTimerQuery()
	{
		auto Native = mDevice->CreateTimerQuery();
		if (!Native)
		{
			return Failure<FArdaRHITimerQueryRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHITimerQueryRef(
		            new FArdaTimerQuery("TimerQuery", eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHIGpuFenceRef> FArdaRHIDeviceImpl::CreateGpuFence()
	{
		auto Native = mDevice->CreateGpuFence();
		if (!Native)
		{
			return Failure<FArdaRHIGpuFenceRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIGpuFenceRef(new FArdaGpuFence("GpuFence", eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::SignalEventQuery(const FArdaRHIEventQueryRef& Query, EArdaRHIQueueType Queue)
	{
		auto* Native = Cast<FArdaEventQuery>(Query.Get());
		if (!Native || !Owns(Native))
		{
			return WrongDevice();
		}
		return mDevice->SignalEventQuery(Native->mNative, Queue);
	}

	TArdaRHIResult<bool> FArdaRHIDeviceImpl::PollEventQuery(const FArdaRHIEventQueryRef& Query)
	{
		auto* Native = Cast<FArdaEventQuery>(Query.Get());
		if (!Native || !Owns(Native))
		{
			return Failure<bool>(WrongDevice());
		}
		return mDevice->PollEventQuery(Native->mNative);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::WaitEventQuery(const FArdaRHIEventQueryRef& Query)
	{
		auto* Native = Cast<FArdaEventQuery>(Query.Get());
		if (!Native || !Owns(Native))
		{
			return WrongDevice();
		}
		return mDevice->WaitEventQuery(Native->mNative);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::ResetEventQuery(const FArdaRHIEventQueryRef& Query)
	{
		auto* Native = Cast<FArdaEventQuery>(Query.Get());
		if (!Native || !Owns(Native))
		{
			return WrongDevice();
		}
		return mDevice->ResetEventQuery(Native->mNative);
	}

	TArdaRHIResult<bool> FArdaRHIDeviceImpl::PollTimerQuery(const FArdaRHITimerQueryRef& Query)
	{
		auto* Native = Cast<FArdaTimerQuery>(Query.Get());
		if (!Native || !Owns(Native))
		{
			return Failure<bool>(WrongDevice());
		}
		return mDevice->PollTimerQuery(Native->mNative);
	}

	TArdaRHIResult<float> FArdaRHIDeviceImpl::GetTimerQuerySeconds(const FArdaRHITimerQueryRef& Query)
	{
		auto* Native = Cast<FArdaTimerQuery>(Query.Get());
		if (!Native || !Owns(Native))
		{
			return Failure<float>(WrongDevice());
		}
		return mDevice->GetTimerQuerySeconds(Native->mNative);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::ResetTimerQuery(const FArdaRHITimerQueryRef& Query)
	{
		auto* Native = Cast<FArdaTimerQuery>(Query.Get());
		if (!Native || !Owns(Native))
		{
			return WrongDevice();
		}
		return mDevice->ResetTimerQuery(Native->mNative);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::SignalGpuFence(const FArdaRHIGpuFenceRef& Fence, EArdaRHIQueueType Queue)
	{
		auto* Native = Cast<FArdaGpuFence>(Fence.Get());
		if (!Native || !Owns(Native))
		{
			return WrongDevice();
		}
		return mDevice->SignalGpuFence(Native->mNative, Queue);
	}

	TArdaRHIResult<bool> FArdaRHIDeviceImpl::PollGpuFence(const FArdaRHIGpuFenceRef& Fence)
	{
		auto* Native = Cast<FArdaGpuFence>(Fence.Get());
		if (!Native || !Owns(Native))
		{
			return Failure<bool>(WrongDevice());
		}
		return mDevice->PollGpuFence(Native->mNative);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::WaitGpuFence(const FArdaRHIGpuFenceRef& Fence)
	{
		auto* Native = Cast<FArdaGpuFence>(Fence.Get());
		if (!Native || !Owns(Native))
		{
			return WrongDevice();
		}
		return mDevice->WaitGpuFence(Native->mNative);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::ResetGpuFence(const FArdaRHIGpuFenceRef& Fence)
	{
		auto* Native = Cast<FArdaGpuFence>(Fence.Get());
		if (!Native || !Owns(Native))
		{
			return WrongDevice();
		}
		return mDevice->ResetGpuFence(Native->mNative);
	}

	TArdaRHIResult<FArdaRHICommandListRef> FArdaRHIDeviceImpl::CreateCommandList(EArdaRHIQueueType Queue,
	    bool bImmediateExecution)
	{
		auto Native = mDevice->CreateCommandList(Queue, bImmediateExecution);
		if (!Native)
		{
			return Failure<FArdaRHICommandListRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHICommandListRef(new FArdaCommandList(this, Queue, eastl::move(Native.mValue), mLifetimeTracker)),
		    {}};
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::CompleteBufferCopies(FArdaCopyBatch Pending, const FArdaRHIStatus& Status)
	{
		FArdaRHIStatus FirstError = Status;

		for (auto& Completion : Pending)
		{
			FArdaRHIStatus CopyStatus = Status;
			FArdaRHIBufferReadbackResult ReadbackResult;
			if (CopyStatus && Completion.mReadbackBuffer)
			{
				try
				{
					ReadbackResult.mValue.resize(Completion.mByteSize);
					auto Mapping = mDevice->MapBuffer(Completion.mReadbackBuffer, 0, Completion.mByteSize);
					if (Mapping)
					{
						std::memcpy(ReadbackResult.mValue.data(), Mapping.mValue, Completion.mByteSize);
						mDevice->UnmapBuffer(Completion.mReadbackBuffer);
					}
					else
					{
						CopyStatus = eastl::move(Mapping.mStatus);
					}
				}
				catch (const std::bad_alloc&)
				{
					CopyStatus =
					    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Could not allocate readback bytes.");
				}
				if (!CopyStatus)
				{
					ReadbackResult.mValue.clear();
				}
			}
			ReadbackResult.mStatus = CopyStatus;

			if (Completion.mOutput)
			{
				if (CopyStatus)
				{
					*Completion.mOutput = eastl::move(ReadbackResult.mValue);
				}
				else
				{
					Completion.mOutput->clear();
				}
			}
			try
			{
				if (Completion.mUploadCallback)
				{
					Completion.mUploadCallback(CopyStatus);
				}
				if (Completion.mReadbackCallback)
				{
					Completion.mReadbackCallback(eastl::move(ReadbackResult));
				}
			}
			catch (...)
			{
				if (CopyStatus)
				{
					CopyStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
					    "A buffer-copy completion callback threw an exception.");
				}
			}
			if (FirstError && !CopyStatus)
			{
				FirstError = CopyStatus;
			}
		}
		return FirstError;
	}

	void FArdaRHIDeviceImpl::RetireBufferCopies(uint64_t Submission, const FArdaRHIStatus& Status)
	{
		FArdaCopyBatch Pending;
		{
			std::lock_guard<std::mutex> Lock(mCopyMutex);
			const auto Found = mPendingCopies.find(Submission);
			if (Found == mPendingCopies.end())
			{
				return;
			}
			Pending = eastl::move(Found->second);
			mPendingCopies.erase(Found);
		}
		(void)CompleteBufferCopies(eastl::move(Pending), Status);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::WaitForSubmission(uint64_t Submission)
	{
		auto Status = mDevice->WaitForSubmission(Submission);
		RetireBufferCopies(Submission, Status);
		return Status;
	}

	TArdaRHIResult<bool> FArdaRHIDeviceImpl::PollSubmission(uint64_t Submission)
	{
		auto Ready = mDevice->PollSubmission(Submission);
		if (Ready ? Ready.mValue : Ready.mStatus.mCode != EArdaRHIResult::Unsupported)
		{
			RetireBufferCopies(Submission, Ready.mStatus);
		}
		return Ready;
	}

	void FArdaRHIDeviceImpl::PollBufferCopies()
	{
		eastl::vector<uint64_t> Submissions;
		{
			std::lock_guard<std::mutex> Lock(mCopyMutex);
			Submissions.reserve(mPendingCopies.size());
			for (const auto& Entry : mPendingCopies)
			{
				Submissions.push_back(Entry.first);
			}
		}
		for (uint64_t Submission : Submissions)
		{
			(void)PollSubmission(Submission);
		}
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::WaitForIdle()
	{
		decltype(mPendingCopies) Pending;
		{
			std::lock_guard<std::mutex> Lock(mCopyMutex);
			Pending.swap(mPendingCopies);
		}
		const auto Status = mDevice->WaitForIdle();
		for (auto& Entry : Pending)
		{
			(void)CompleteBufferCopies(eastl::move(Entry.second), Status);
		}
		return Status;
	}

	TArdaRHIResult<uint64_t> FArdaRHIDeviceImpl::FinishCommandListSubmission(FArdaCommandList& CommandList,
	    TArdaRHIResult<uint64_t> Submitted)
	{
		auto Pending = CommandList.TakeCopyCompletions();
		if (Pending.empty())
		{
			return Submitted;
		}
		const bool Blocking = eastl::any_of(Pending.begin(),
		    Pending.end(),
		    [](const auto& Copy)
		    {
			    return Copy.mbBlocking;
		    });
		if (Submitted && Submitted.mValue && !Blocking)
		{
			try
			{
				std::lock_guard<std::mutex> Lock(mCopyMutex);
				auto& Batch = mPendingCopies[Submitted.mValue];
				if (Batch.empty())
				{
					Batch.swap(Pending);
				}
				else
				{
					Batch.reserve(Batch.size() + Pending.size());
					for (auto& Copy : Pending)
					{
						Batch.push_back(eastl::move(Copy));
					}
				}
				return Submitted;
			}
			catch (const std::bad_alloc&)
			{
				// The provider accepted this work. Finish it inline if retaining the batch fails.
			}
		}
		const auto Status =
		    Submitted && Submitted.mValue ? mDevice->WaitForSubmission(Submitted.mValue) : Submitted.mStatus;
		const auto Completed = CompleteBufferCopies(eastl::move(Pending), Status);
		return Completed ? Submitted : Failure<uint64_t>(Completed);
	}

	TArdaRHIResult<uint64_t> FArdaRHIDeviceImpl::ExecuteCommandList(const FArdaRHICommandListRef& CommandList)
	{
		auto* Native = Cast<FArdaCommandList>(CommandList.Get());
		if (!Native || !Owns(Native))
		{
			return Failure<uint64_t>(WrongDevice());
		}
		if (const FArdaRHIStatus Status = Native->ValidateFacadeStartStates(); !Status)
		{
			return Failure<uint64_t>(Status);
		}
		auto Submitted = mDevice->ExecuteCommandList(Native->GetNative(), Native->GetQueueType());
		if (Submitted)
		{
			Native->CommitFacadeStates();
		}
		return FinishCommandListSubmission(*Native, eastl::move(Submitted));
	}

	TArdaRHIResult<uint64_t> FArdaRHIDeviceImpl::ExecuteCommandLists(
	    const eastl::vector<FArdaRHICommandListRef>& CommandLists,
	    EArdaRHIQueueType Queue)
	{
		if (CommandLists.empty())
		{
			return Failure<uint64_t>(Invalid("At least one command list is required."));
		}
		uint64_t Last = 0;
		for (const auto& CommandList : CommandLists)
		{
			auto* Native = Cast<FArdaCommandList>(CommandList.Get());
			if (!Native || !Owns(Native) || Native->GetQueueType() != Queue)
			{
				return Failure<uint64_t>(WrongDevice());
			}
			auto Submitted = ExecuteCommandList(CommandList);
			if (!Submitted)
			{
				return Submitted;
			}
			Last = Submitted.mValue;
		}
		return {Last, {}};
	}
}
