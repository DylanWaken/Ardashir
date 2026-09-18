#include <cstring>
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
		return {
		    FArdaRHIGpuFenceRef(new FArdaGpuFence("GpuFence", eastl::move(Native.mValue), this, mLifetimeTracker)),
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
		return {
		    FArdaRHICommandListRef(new FArdaCommandList(this, Queue, eastl::move(Native.mValue), mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<uint64_t> FArdaRHIDeviceImpl::FinishCommandListSubmission(FArdaCommandList& CommandList,
	    TArdaRHIResult<uint64_t> Submitted)
	{
		auto Completions = CommandList.TakeCopyCompletions();
		if (Completions.empty())
		{
			return Submitted;
		}

		const bool bBlocking = eastl::any_of(Completions.begin(),
		    Completions.end(),
		    [](const FArdaPendingBufferCopyCompletion& Completion)
		    {
			    return Completion.mbBlocking;
		    });
		auto ProviderDevice = mDevice;
		const uint64_t Submission = Submitted.mValue;
		const FArdaRHIStatus SubmissionStatus = Submitted.mStatus;
		const auto Complete = [ProviderDevice, Submission, SubmissionStatus](
		                          eastl::vector<FArdaPendingBufferCopyCompletion> Pending)
		{
			FArdaRHIStatus WaitStatus = SubmissionStatus;
			if (WaitStatus && Submission != 0)
			{
				WaitStatus = ProviderDevice->WaitForSubmission(Submission);
			}
			FArdaRHIStatus FirstError = WaitStatus;

			for (auto& Completion : Pending)
			{
				FArdaRHIStatus CopyStatus = WaitStatus;
				FArdaRHIBufferReadbackResult ReadbackResult;
				ReadbackResult.mStatus = CopyStatus;
				if (CopyStatus && Completion.mReadbackBuffer)
				{
					auto Mapping = ProviderDevice->MapBuffer(Completion.mReadbackBuffer, 0, Completion.mByteSize);
					if (!Mapping)
					{
						CopyStatus = eastl::move(Mapping.mStatus);
						ReadbackResult.mStatus = CopyStatus;
					}
					else
					{
						ReadbackResult.mValue.resize(Completion.mByteSize);
						std::memcpy(ReadbackResult.mValue.data(), Mapping.mValue, Completion.mByteSize);
						ProviderDevice->UnmapBuffer(Completion.mReadbackBuffer);
					}
				}

				if (Completion.mOutput)
				{
					if (CopyStatus)
					{
						*Completion.mOutput = ReadbackResult.mValue;
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
		};

		if (bBlocking)
		{
			const FArdaRHIStatus CompletionStatus = Complete(eastl::move(Completions));
			if (!CompletionStatus)
			{
				return Failure<uint64_t>(CompletionStatus);
			}
			return Submitted;
		}

		std::thread(
		    [Complete, Pending = eastl::move(Completions)]() mutable
		    {
			    (void)Complete(eastl::move(Pending));
		    })
		    .detach();
		return Submitted;
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
