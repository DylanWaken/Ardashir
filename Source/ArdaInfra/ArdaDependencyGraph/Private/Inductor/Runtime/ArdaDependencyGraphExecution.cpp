#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Graph/Node/ArdaGraphRecordingScope.h"
#include "Inductor/Runtime/ArdaInductorRuntime.h"
#include "Inductor/Runtime/ArdaInductorFrame.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"

namespace arda
{
	TArdaRHIResult<FArdaDependencyFrameTicket> FArdaDependencyGraph::Submit(const FArdaGraphExecuteOptions& Options)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return {{}, Status};
		}
		std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
		auto& State = *mRevisions;
		if (mImpl->mbEditing || State.mActiveEdits != 0)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Submit requires closed edits.")};
		}
		if (mImpl->mbCompiling || mImpl->mbPreprocessing || mImpl->mbExecuting)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Submit requires an unlocked execution stage.")};
		}
		if (State.mbBuilding || State.mPersistent != mImpl || !mImpl->mCompile.mRevision)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			        "Submit requires the compiled construction; pending edits need another Compile call.")};
		}
		// Capture the pre-attempt revision state, matching the staged compile lock.
		FArdaGraphRecordingScope RecordingScope(mRevisions.get(), State.Snapshot());
		// The attempt consumes the staged compile lock even when recording or submission fails,
		// so the caller can repair the graph without an explicit discard.
		State.mbLocked = false;
		auto Result = FArdaInductorRuntime::Submit(*mImpl, Options);
		State.mNextFrameSequence = eastl::max(State.mNextFrameSequence, mImpl->mCompile.mExecution->mNextFrameSequence);
		return Result;
	}

	TArdaRHIResult<FArdaDependencyFrameTicket> FArdaDependencyGraph::SubmitAndWait(
	    const FArdaGraphExecuteOptions& Options)
	{
		return Submit(Options);
	}

	FArdaGraphExecutionResult FArdaDependencyGraph::Execute(const FArdaGraphExecuteOptions& Options)
	{
		// Keep the construction stable for the complete frame and block authoring while GPU work
		// completes, matching the synchronous execution contract.
		std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
		const auto Submitted = Submit(Options);
		if (!Submitted.mValue)
		{
			FArdaGraphExecutionResult Result;
			Result.mStatus = Submitted.mStatus;
			return Result;
		}
		return Wait(Submitted.mValue);
	}

	FArdaGraphExecutionResult FArdaDependencyGraph::ExecuteAndWait(const FArdaGraphExecuteOptions& Options)
	{
		return Execute(Options);
	}

	FArdaGraphExecutionResult FArdaDependencyGraph::Wait(const FArdaDependencyFrameTicket& Ticket)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			FArdaGraphExecutionResult Result;
			Result.mStatus = Status;
			return Result;
		}
		uint64_t Identity;
		{
			std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
			Identity = mImpl->mIdentity;
		}
		return FArdaInductorRuntime::Wait(Identity, Ticket);
	}

	TArdaRHIResult<bool> FArdaDependencyGraph::IsComplete(const FArdaDependencyFrameTicket& Ticket) const
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return {false, Status};
		}
		uint64_t Identity;
		{
			std::unique_lock<std::recursive_mutex> Lock(mRevisions->mMutex, std::try_to_lock);
			if (!Lock.owns_lock())
			{
				return {false, {}};
			}
			Identity = mImpl->mIdentity;
		}
		return FArdaInductorRuntime::IsComplete(Identity, Ticket);
	}

	eastl::vector<FArdaInductorTimingSample> FArdaDependencyGraph::GetTimingProfile() const
	{
		FArdaInductorRuntime::CollectTelemetry(*mImpl);
		return mImpl->mCompile.mExecution->mTiming->Snapshot();
	}

	eastl::vector<FArdaInductorNodeTiming> FArdaDependencyGraph::GetTimingHistory() const
	{
		FArdaInductorRuntime::CollectTelemetry(*mImpl);
		return mImpl->mCompile.mExecution->mTiming->History();
	}

	void FArdaDependencyGraph::CollectTelemetry()
	{
		AdvanceArdaInductorTelemetry(*mImpl);
	}

	FArdaInductorAdaptiveSchedulingStats FArdaDependencyGraph::GetAdaptiveSchedulingStats() const
	{
		const auto& Execution = *mImpl->mCompile.mExecution;
		auto Stats = Execution.mAdaptiveStats;
		Stats.mbPending = bool(Execution.mPendingSchedule);
		return Stats;
	}

	void FArdaDependencyGraph::ClearTimingProfile()
	{
		mImpl->mCompile.mExecution->mTiming->Clear();
	}

	FArdaCudaGraphStats FArdaDependencyGraph::GetCudaGraphStats() const
	{
		FArdaCudaGraphStats Result;
		if (!mImpl->mCompile.mExecution->mRuntime)
		{
			return Result;
		}
		for (const auto& Frame : mImpl->mCompile.mExecution->mRuntime->mFrames)
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
