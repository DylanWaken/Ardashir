#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "Inductor/Compiler/ArdaInductorCommandProgramInternal.h"
#include "Inductor/Runtime/ArdaInductorRuntimeStatus.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <algorithm>

#include <EASTL/unordered_set.h>
#include <cstring>

namespace arda
{
	FArdaRHIDeviceToHostCopyCallback FArdaInductorReadbackCompletions::Register(
	    eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
	    uint32_t PassIndex)
	{
		auto Promise = eastl::make_shared<std::promise<FArdaRHIBufferReadbackResult>>();
		FArdaPending Pending{Promise->get_future(), eastl::move(Destination), PassIndex};
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			mPending.push_back(eastl::move(Pending));
		}
		return [Promise = eastl::move(Promise)](FArdaRHIBufferReadbackResult Result)
		{
			Promise->set_value(eastl::move(Result));
		};
	}

	FArdaRHIStatus FArdaInductorReadbackCompletions::Drain(FArdaRHIStatus SubmissionStatus)
	{
		eastl::vector<FArdaPending> Pending;
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			Pending.swap(mPending);
		}
		// Parallel recording order is not semantic order. Multiple nodes targeting
		// one host vector publish in the compiled pass order on this calling thread.
		// EASTL's stable sort copies elements; pending std::future values are move-only.
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
				Result.mStatus = MakeArdaInductorRuntimeError(
				    "Readback completion was discarded before its command list completed.");
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
}
