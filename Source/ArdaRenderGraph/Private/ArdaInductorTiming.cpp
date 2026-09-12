#include "ArdaDependencyGraphInternal.h"
#include <cmath>
#include <condition_variable>
#include <thread>

namespace arda
{
	void FArdaInductorTimingAccumulator::Configure(double Alpha, uint32_t HistoryCapacity)
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		mAlpha = Alpha;
		mHistoryCapacity = HistoryCapacity;
		mHistory.clear();
		mHistoryCursor = 0;
	}

	void FArdaInductorTimingAccumulator::Add(const eastl::string& Name,
	    double Seconds,
	    const eastl::vector<FArdaGraphNodeHandle>& Nodes,
	    uint64_t Sequence,
	    EArdaRHIQueueType Queue,
	    uint64_t Epoch)
	{
		if (!std::isfinite(Seconds) || Seconds < 0)
		{
			return;
		}
		eastl::string Key = Nodes.empty() ? "name:" : "nodes:";
		if (Nodes.empty())
		{
			Key += Name;
		}
		for (const auto Node : Nodes)
		{
			for (uint64_t V : {Node.GetGraphIdentity(), uint64_t(Node.GetIndex()), uint64_t(Node.GetGeneration())})
			{
				Key.append(reinterpret_cast<const char*>(&V), sizeof(V));
			}
		}
		std::lock_guard<std::mutex> Lock(mMutex);
		if (Epoch != UINT64_MAX && Epoch != mEpoch)
		{
			return;
		}
		auto& Sample = mSamples[Key];
		// Native records are consumed once by their frame receipt. A late receipt
		// still contributes to collection-order statistics and bounded history.
		const bool LatestFrame =
		    !Sample.mSampleCount || Sequence > Sample.mLastFrameSequence || (!Sequence && !Sample.mLastFrameSequence);
		Sample.mNodeName = Name;
		Sample.mNodes = Nodes;
		Sample.mGpuSeconds =
		    Sample.mSampleCount ? Sample.mGpuSeconds + mAlpha * (Seconds - Sample.mGpuSeconds) : Seconds;
		Sample.mMinGpuSeconds = Sample.mSampleCount ? eastl::min(Sample.mMinGpuSeconds, Seconds) : Seconds;
		Sample.mMaxGpuSeconds = eastl::max(Sample.mMaxGpuSeconds, Seconds);
		if (LatestFrame)
		{
			Sample.mLastGpuSeconds = Seconds;
			Sample.mLastFrameSequence = Sequence;
			Sample.mQueue = Queue;
		}
		if (Sample.mSampleCount != UINT32_MAX)
		{
			++Sample.mSampleCount;
		}
		if (mHistoryCapacity && Nodes.size() == 1)
		{
			FArdaInductorNodeTiming Record{Nodes.front(), Name, Sequence, Seconds, Queue};
			if (mHistory.size() < mHistoryCapacity)
			{
				mHistory.push_back(eastl::move(Record));
			}
			else
			{
				mHistory[mHistoryCursor] = eastl::move(Record);
				mHistoryCursor = (mHistoryCursor + 1) % mHistoryCapacity;
			}
		}
	}

	eastl::vector<FArdaInductorTimingSample> FArdaInductorTimingAccumulator::Snapshot() const
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		eastl::vector<FArdaInductorTimingSample> Result;
		for (const auto& Pair : mSamples)
		{
			Result.push_back(Pair.second);
		}
		eastl::sort(Result.begin(),
		    Result.end(),
		    [](const auto& A, const auto& B)
		    {
			    return A.mNodeName != B.mNodeName ? A.mNodeName < B.mNodeName : A.mNodes < B.mNodes;
		    });
		return Result;
	}

	eastl::vector<FArdaInductorNodeTiming> FArdaInductorTimingAccumulator::History() const
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		auto Result = mHistory;
		eastl::sort(Result.begin(),
		    Result.end(),
		    [](const auto& A, const auto& B)
		    {
			    return A.mFrameSequence != B.mFrameSequence ? A.mFrameSequence < B.mFrameSequence : A.mNode < B.mNode;
		    });
		return Result;
	}

	uint64_t FArdaInductorTimingAccumulator::GetEpoch() const
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		return mEpoch;
	}

	void FArdaInductorTimingAccumulator::Clear()
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		mSamples.clear();
		mHistory.clear();
		mHistoryCursor = 0;
		++mEpoch;
	}

	namespace
	{
		bool SubmissionsReady(FArdaDependencyFrameTicket::FState& State)
		{
			for (auto Instance : State.mExecution.mLastSubmittedInstances)
			{
				if (Instance)
				{
					const auto Ready = State.mDevice->PollSubmission(Instance);
					if (!Ready || !Ready.mValue)
					{
						return false;
					}
				}
			}
			for (const auto& Fence : State.mRecoveryFences)
			{
				const auto Ready = State.mDevice->PollGpuFence(Fence);
				if (!Ready || !Ready.mValue)
				{
					return false;
				}
			}
			return true;
		}

		auto OrderedStates(FArdaDependencyGraph::FImpl& Graph)
		{
			eastl::vector<eastl::shared_ptr<FArdaDependencyFrameTicket::FState>> States;
			if (Graph.mRuntime)
			{
				for (const auto& Frame : Graph.mRuntime->mFrames)
				{
					if (Frame->mActive)
					{
						States.push_back(Frame->mActive);
					}
					if (Frame->mTimingState && Frame->mTimingState != Frame->mActive)
					{
						States.push_back(Frame->mTimingState);
					}
				}
			}
			eastl::sort(States.begin(),
			    States.end(),
			    [](const auto& A, const auto& B)
			    {
				    return A->mSequence < B->mSequence;
			    });
			return States;
		}
	}

	// The caller owns State.mMutex. Query availability is checked before reading durations.
	void FArdaInductorRuntime::CollectStateTelemetry(FArdaDependencyFrameTicket::FState& State)
	{
		if (!State.mbSampleTiming || !State.mTiming || !State.mExecution.mStatus)
		{
			return;
		}
		if (!SubmissionsReady(State))
		{
			return;
		}
		for (auto I = State.mTimers.begin(); I != State.mTimers.end();)
		{
			const auto Ready = State.mDevice->PollTimerQuery(I->mQuery);
			if (Ready && !Ready.mValue)
			{
				++I;
				continue;
			}
			if (Ready)
			{
				const auto Seconds = State.mDevice->GetTimerQuerySeconds(I->mQuery);
				if (Seconds)
				{
					State.mTiming
					    ->Add(I->mName, Seconds.mValue, I->mNodes, State.mSequence, I->mQueue, State.mTimingEpoch);
				}
			}
			I = State.mTimers.erase(I);
		}
		for (auto I = State.mCudaTimers.begin(); I != State.mCudaTimers.end();)
		{
			const auto Sample = I->mQuery->Poll(true);
			if (Sample && !Sample.mValue.mbReady)
			{
				++I;
				continue;
			}
			if (Sample)
			{
				for (const auto& Region : Sample.mValue.mRegions)
				{
					for (size_t N = 0; N < I->mNodes.size(); ++N)
					{
						if (I->mNodes[N].GetIndex() == Region.mRegionId)
						{
							State.mTiming->Add(I->mNames[N],
							    Region.mGpuSeconds,
							    {I->mNodes[N]},
							    State.mSequence,
							    I->mQueue,
							    State.mTimingEpoch);
						}
					}
				}
			}
			I = State.mCudaTimers.erase(I);
		}
	}

	void FArdaInductorRuntime::CollectTelemetry(FArdaDependencyGraph::FImpl& Graph)
	{
		if (!Graph.mOptions.mbEnableGpuTiming || Graph.mbEditing || Graph.mbExecuting)
		{
			return;
		}
		for (const auto& State : OrderedStates(Graph))
		{
			std::unique_lock<std::mutex> Lock(State->mMutex, std::try_to_lock);
			if (Lock.owns_lock())
			{
				CollectStateTelemetry(*State);
			}
		}
	}

	bool FArdaInductorRuntime::TryRetireAll(FArdaDependencyGraph::FImpl& Graph)
	{
		for (const auto& State : OrderedStates(Graph))
		{
			std::unique_lock<std::mutex> Lock(State->mMutex, std::try_to_lock);
			if (!Lock.owns_lock())
			{
				return false;
			}
			if (!State->mExecution.mStatus || !State->mRetirementStatus)
			{
				return false;
			}
			if (State->mbComplete)
			{
				CollectStateTelemetry(*State);
				if (!State->mTimers.empty() || !State->mCudaTimers.empty())
				{
					return false;
				}
				continue;
			}
			if (!SubmissionsReady(*State) || !State->mReadbackCompletions.IsReady())
			{
				return false;
			}
			CollectStateTelemetry(*State);
			State->mExecution.mStatus = State->mReadbackCompletions.Drain(State->mExecution.mStatus);
			State->mRecoveryFences.clear();
			State->mbComplete = true;
			if (!State->mExecution.mStatus)
			{
				return false;
			}
			if (!State->mTimers.empty() || !State->mCudaTimers.empty())
			{
				return false;
			}
		}
		return true;
	}

	eastl::shared_ptr<FArdaDependencyGraph::FImpl> CloneArdaInductorTimingInputs(const FArdaDependencyGraph::FImpl& G)
	{
		auto S = eastl::make_shared<FArdaDependencyGraph::FImpl>();
		S->mDevice = G.mDevice;
		S->mIdentity = G.mIdentity;
		S->mOptions = G.mOptions;
		S->mTopology = G.mTopology.CloneSnapshot();
		S->mResources = G.mResources;
		S->mResourceGenerations = G.mResourceGenerations;
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
		std::atomic<bool> mbDone{false}, mbCanceled{false};
		std::mutex mMutex;
		std::condition_variable mCompleted;
		uint64_t mRevision = 0, mEpoch = 0;
		FArdaRHIStatus mStatus;
		FArdaInductorAdaptiveScheduleState mSearch;
		FArdaInductorAdaptiveScheduleResult mResult;
		eastl::shared_ptr<FArdaDependencyGraph::FImpl> mSnapshot;
	};

	FArdaRHIStatus RequestArdaInductorTimingOptimization(FArdaDependencyGraph::FImpl& G, bool Automatic)
	{
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
					    Job->mbDone.store(true, std::memory_order_release);
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

	void AdvanceArdaInductorTelemetry(FArdaDependencyGraph::FImpl& G)
	{
		if (G.mbEditing || G.mbExecuting || !G.mOptions.mbEnableGpuTiming)
		{
			return;
		}
		FArdaInductorRuntime::CollectTelemetry(G);
		if (G.mAdaptiveJob && G.mAdaptiveJob->mbDone.load(std::memory_order_acquire))
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
		if (G.mOptions.mbEnableAdaptiveScheduling && !G.mAdaptiveJob &&
		    G.mNextFrameSequence - G.mLastAdaptiveSequence >= G.mOptions.mAdaptiveSchedulingInterval)
		{
			(void)RequestArdaInductorTimingOptimization(G, true);
		}
	}

	void CancelArdaInductorTimingOptimization(FArdaDependencyGraph::FImpl& G)
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
			    return Job->mbDone.load(std::memory_order_acquire);
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
