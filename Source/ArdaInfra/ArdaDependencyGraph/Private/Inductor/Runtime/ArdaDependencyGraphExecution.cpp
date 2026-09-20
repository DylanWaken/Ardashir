#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Inductor/Runtime/ArdaInductorRuntime.h"
#include "Inductor/Runtime/ArdaInductorFrame.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"

namespace arda
{
	FArdaGraphExecutionResult FArdaDependencyGraph::Execute(const FArdaGraphExecuteOptions& Options)
	{
		auto Submitted = Submit(Options);
		if (Submitted.mValue)
		{
			return Wait(Submitted.mValue);
		}
		FArdaGraphExecutionResult Result;
		Result.mStatus = Submitted.mStatus;
		return Result;
	}

	TArdaRHIResult<FArdaDependencyFrameTicket> FArdaDependencyGraph::Submit(const FArdaGraphExecuteOptions& Options)
	{
		if (IsEditing() || mImpl->mbCompiling || mImpl->mbExecuting || mRevisions->mbBuilding ||
		    !mRevisions->mPersistent || !mImpl->mCompile.mRevision)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			        "Submit requires closed edits staged by a successful Compile call.")};
		}
		// Submission finishes CPU recording before returning. Further edits build a separate draft;
		// the persistent snapshot and any outstanding frame tickets continue to own their execution data.
		auto Submitted = FArdaInductorRuntime::Submit(*mImpl, Options);
		mRevisions->mbLocked = false;
		mRevisions->mNextFrameSequence = eastl::max(mRevisions->mNextFrameSequence, mImpl->mNextFrameSequence);
		return Submitted;
	}

	FArdaGraphExecutionResult FArdaDependencyGraph::Wait(const FArdaDependencyFrameTicket& Ticket)
	{
		return FArdaInductorRuntime::Wait(*mImpl, Ticket);
	}

	TArdaRHIResult<bool> FArdaDependencyGraph::IsComplete(const FArdaDependencyFrameTicket& Ticket) const
	{
		return FArdaInductorRuntime::IsComplete(*mImpl, Ticket);
	}

	eastl::vector<FArdaInductorTimingSample> FArdaDependencyGraph::GetTimingProfile() const
	{
		FArdaInductorRuntime::CollectTelemetry(*mImpl);
		return mImpl->mTiming->Snapshot();
	}

	eastl::vector<FArdaInductorNodeTiming> FArdaDependencyGraph::GetTimingHistory() const
	{
		FArdaInductorRuntime::CollectTelemetry(*mImpl);
		return mImpl->mTiming->History();
	}

	void FArdaDependencyGraph::CollectTelemetry()
	{
		AdvanceArdaInductorTelemetry(*mImpl);
	}

	FArdaInductorAdaptiveSchedulingStats FArdaDependencyGraph::GetAdaptiveSchedulingStats() const
	{
		return mImpl->mAdaptiveStats;
	}

	void FArdaDependencyGraph::ClearTimingProfile()
	{
		mImpl->mTiming->Clear();
	}

	FArdaCudaGraphStats FArdaDependencyGraph::GetCudaGraphStats() const
	{
		FArdaCudaGraphStats Result;
		if (!mImpl->mRuntime)
		{
			return Result;
		}
		for (const auto& Frame : mImpl->mRuntime->mFrames)
		{
			for (const auto& Entry : Frame->mCudaCaches)
			{
				const auto Stats = Entry.second->GetStats();
				Result.mCaptureCount += Stats.mCaptureCount;
				Result.mReplayCount += Stats.mReplayCount;
				Result.mRebuildCount += Stats.mRebuildCount;
				Result.mFallbackCount += Stats.mFallbackCount;
				Result.mCaptureFailureCount += Stats.mCaptureFailureCount;
				Result.mCacheHitCount += Stats.mCacheHitCount;
				Result.mEvictionCount += Stats.mEvictionCount;
				Result.mCachedVariantCount += Stats.mCachedVariantCount;
				if (!Stats.mLastFallbackReason.empty())
				{
					Result.mLastFallbackReason = Stats.mLastFallbackReason;
				}
			}
		}
		return Result;
	}
}
