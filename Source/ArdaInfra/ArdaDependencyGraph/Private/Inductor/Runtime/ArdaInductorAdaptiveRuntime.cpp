#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Runtime/ArdaInductorFrame.h"
#include "Inductor/Runtime/ArdaInductorFrameTicketState.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"
#include "Inductor/Rules/ArdaInductorAdaptiveSchedule.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/atomic.h>
#include <cmath>
#include <condition_variable>
#include <thread>

namespace arda
{
	eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> CloneArdaInductorTimingInputs(
	    const FArdaDependencyGraph::FArdaImpl& G)
	{
		auto S = eastl::make_shared<FArdaDependencyGraph::FArdaImpl>();
		S->mDevice = G.mDevice;
		S->mIdentity = G.mIdentity;
		S->mOptions = G.mOptions;
		S->mTopology = G.mTopology.CloneSnapshot();
		S->mResources = G.mResources;
		S->mResourceGenerations = G.mResourceGenerations;
		S->mResourceAliases = G.mResourceAliases;
		S->mCompiledResourceAliases = G.mCompiledResourceAliases;
		S->mNames = G.mNames;
		S->mResourceNames = G.mResourceNames;
		S->mManualEdges = G.mManualEdges;
		S->mCompile = G.mCompile;
		S->mCostOverrides = G.mCostOverrides;
		S->mCostOverrideKeys = G.mCostOverrideKeys;
		S->mCostOverrideDefinitions = G.mCostOverrideDefinitions;
		S->mTiming = G.mTiming;
		return S;
	}

	struct FArdaInductorAdaptiveJob
	{
		eastl::atomic<bool> mbDone{false}, mbCanceled{false};
		std::mutex mMutex;
		std::condition_variable mCompleted;
		uint64_t mRevision = 0, mEpoch = 0;
		FArdaRHIStatus mStatus;
		FArdaInductorAdaptiveScheduleState mSearch;
		FArdaInductorAdaptiveScheduleResult mResult;
		eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> mSnapshot;
	};

	FArdaRHIStatus RequestArdaInductorTimingOptimization(FArdaDependencyGraph::FArdaImpl& G, bool Automatic)
	{
		if (!AllowsArdaInductorTimingOptimization(G.mOptions))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Timing optimization requires every selected graph rule to permit timing-based reordering.");
		}
		if (G.mbEditing || G.mbExecuting || !G.mRuntime || !G.mRuntime->mAdaptiveSeed)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Timing optimization requires a compiled graph with telemetry enabled.");
		}
		if (G.mAdaptiveJob)
		{
			return {};
		}
		const auto Profile = G.mTiming->Snapshot();
		const auto Required = Automatic ? G.mOptions.mAdaptiveSchedulingMinSamples : 1u;
		bool HasSamples = false;
		for (const auto& P : Profile)
		{
			HasSamples |=
			    P.mNodes.size() == 1 && P.mSampleCount >= Required && std::isfinite(P.mGpuSeconds) && P.mGpuSeconds > 0;
		}
		if (!HasSamples)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "No eligible positive per-node GPU samples are available.");
		}
		auto Job = eastl::make_shared<FArdaInductorAdaptiveJob>();
		Job->mRevision = G.mCompile.mRevision;
		Job->mEpoch = G.mTiming->GetEpoch();
		Job->mSearch = G.mAdaptiveSearch;
		const auto Seed = G.mRuntime->mAdaptiveSeed;
		const auto Reuse = G.mRuntime->mReuseFrames;
		const auto Plan = G.mRuntime->mAdaptivePlan;
		G.mAdaptiveJob = Job;
		G.mAdaptiveStats.mbPending = true;
		G.mLastAdaptiveSequence = G.mNextFrameSequence;
		// Only immutable inputs and retained native objects cross this boundary. Submission never joins workers.
		try
		{
			std::thread(
			    [Job, Source = Seed, Pools = Reuse, Fixed = Plan, Profile, Required]() mutable
			    {
				    try
				    {
					    Job->mSnapshot = CloneArdaInductorTimingInputs(*Source);
					    auto& S = *Job->mSnapshot;
					    double Hints = 0, Seconds = 0;
					    for (const auto& P : Profile)
					    {
						    if (P.mNodes.size() != 1 || P.mSampleCount < Required || !std::isfinite(P.mGpuSeconds) ||
						        P.mGpuSeconds <= 0)
						    {
							    continue;
						    }
						    const auto* N = S.mTopology.TryGetNode(P.mNodes.front());
						    if (!N)
						    {
							    continue;
						    }
						    Hints += N->mPayload.mDesc.mEstimatedCost;
						    Seconds += P.mGpuSeconds;
					    }
					    if (Seconds > 0)
					    {
						    for (const auto& P : Profile)
						    {
							    if (P.mNodes.size() != 1 || P.mSampleCount < Required ||
							        !std::isfinite(P.mGpuSeconds) || P.mGpuSeconds <= 0)
							    {
								    continue;
							    }
							    const auto* N = S.mTopology.TryGetNode(P.mNodes.front());
							    if (!N)
							    {
								    continue;
							    }
							    const auto& Node = N->mPayload;
							    S.mCostOverrides[Node.mName] = P.mGpuSeconds * Hints / Seconds;
							    S.mCostOverrideKeys[Node.mName] = Node.mCanonicalKey;
							    S.mCostOverrideDefinitions[Node.mName] = Node.mDefinition;
						    }
					    }
					    if (!Job->mbCanceled.load())
					    {
						    auto Proposal = TuneArdaInductorSchedule(S,
						        *Fixed,
						        Job->mSearch,
						        S.mOptions.mAdaptiveSchedulingSearchBudget,
						        S.mOptions.mAdaptiveSchedulingMinImprovement);
						    Job->mStatus = Proposal.mStatus;
						    if (Proposal)
						    {
							    Job->mResult = eastl::move(Proposal.mValue);
							    if (Job->mResult.mbImproved && !Job->mbCanceled.load())
							    {
								    S.mCompile = Job->mResult.mCompile;
								    ++S.mCompile.mRevision;
								    Job->mStatus =
								        FArdaInductorRuntime::Prepare(S, Job->mResult.mMemoryPlan, Pools.get());
							    }
						    }
					    }
				    }
				    catch (const std::exception& E)
				    {
					    Job->mStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, E.what());
				    }
				    catch (...)
				    {
					    Job->mStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
					        "Background schedule preparation failed.");
				    }
				    // Release captured GPU ownership before publishing completion. Edits may start
				    // allocating a new pool immediately after cancellation observes this flag.
				    Source.reset();
				    Pools.reset();
				    Fixed.reset();
				    if (Job->mbCanceled.load())
				    {
					    Job->mSnapshot.reset();
				    }
				    {
					    std::lock_guard<std::mutex> Lock(Job->mMutex);
					    Job->mbDone.store(true, eastl::memory_order_release);
				    }
				    Job->mCompleted.notify_all();
			    })
			    .detach();
		}
		catch (const std::exception& E)
		{
			G.mAdaptiveJob.reset();
			G.mAdaptiveStats.mbPending = false;
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, E.what());
		}
		return {};
	}

	void AdvanceArdaInductorTelemetry(FArdaDependencyGraph::FArdaImpl& G)
	{
		if (G.mbEditing || G.mbExecuting || !G.mOptions.mbEnableGpuTiming)
		{
			return;
		}
		FArdaInductorRuntime::CollectTelemetry(G);
		if (G.mAdaptiveJob && G.mAdaptiveJob->mbDone.load(eastl::memory_order_acquire))
		{
			const auto Job = G.mAdaptiveJob;
			const bool Valid = Job->mRevision == G.mCompile.mRevision && Job->mEpoch == G.mTiming->GetEpoch();
			if (Valid && Job->mStatus && Job->mResult.mbImproved && !FArdaInductorRuntime::TryRetireAll(G))
			{
				return;
			}
			++G.mAdaptiveStats.mIterations;
			G.mAdaptiveStats.mCandidatesExamined += Job->mResult.mCandidatesExamined;
			G.mAdaptiveStats.mLastBaselineCost = Job->mResult.mIncumbentCost;
			G.mAdaptiveStats.mLastCandidateCost = Job->mResult.mCandidateCost;
			G.mAdaptiveStats.mLastStatus = Job->mStatus;
			if (Valid && Job->mStatus)
			{
				G.mAdaptiveSearch = Job->mSearch;
				if (Job->mResult.mbImproved && Job->mSnapshot->mRuntime)
				{
					auto& S = *Job->mSnapshot;
					S.mRuntime->mBinding->mGraph = &G;
					G.mRuntime = eastl::move(S.mRuntime);
					G.mCompile = eastl::move(S.mCompile);
					G.mCostOverrides = eastl::move(S.mCostOverrides);
					G.mCostOverrideKeys = eastl::move(S.mCostOverrideKeys);
					G.mCostOverrideDefinitions = eastl::move(S.mCostOverrideDefinitions);
					++G.mAdaptiveStats.mAcceptedSchedules;
				}
			}
			G.mAdaptiveJob.reset();
			G.mAdaptiveStats.mbPending = false;
		}
		if (G.mOptions.mbEnableAdaptiveScheduling && AllowsArdaInductorTimingOptimization(G.mOptions) &&
		    !G.mAdaptiveJob && G.mNextFrameSequence - G.mLastAdaptiveSequence >= G.mOptions.mAdaptiveSchedulingInterval)
		{
			(void)RequestArdaInductorTimingOptimization(G, true);
		}
	}

	void CancelArdaInductorTimingOptimization(FArdaDependencyGraph::FArdaImpl& G)
	{
		if (!G.mAdaptiveJob)
		{
			return;
		}
		auto Job = eastl::move(G.mAdaptiveJob);
		Job->mbCanceled.store(true);
		// Explicit edits/destruction already retire graph work. Join CPU preparation here only,
		// so replacement allocations cannot overlap a worker's retained old VRAM pool.
		std::unique_lock<std::mutex> Lock(Job->mMutex);
		Job->mCompleted.wait(Lock,
		    [&]
		    {
			    return Job->mbDone.load(eastl::memory_order_acquire);
		    });
		Job->mSnapshot.reset();
		Job->mResult.mMemoryPlan = {};
		G.mAdaptiveStats.mbPending = false;
	}

	FArdaRHIStatus FArdaDependencyGraph::OptimizeFromTimingProfile()
	{
		FArdaInductorRuntime::CollectTelemetry(*mImpl);
		return RequestArdaInductorTimingOptimization(*mImpl, false);
	}
}
