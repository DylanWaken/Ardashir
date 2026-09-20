#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Runtime/ArdaInductorFrame.h"
#include "Inductor/Runtime/ArdaInductorFrameTicketState.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cmath>

namespace arda
{
	namespace
	{
		bool SubmissionsReady(FArdaDependencyFrameTicket::FArdaState& State)
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

		auto OrderedStates(FArdaDependencyGraph::FArdaImpl& Graph)
		{
			eastl::vector<eastl::shared_ptr<FArdaDependencyFrameTicket::FArdaState>> States;
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
	void FArdaInductorRuntime::CollectStateTelemetry(FArdaDependencyFrameTicket::FArdaState& State)
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

	void FArdaInductorRuntime::CollectTelemetry(FArdaDependencyGraph::FArdaImpl& Graph)
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

	bool FArdaInductorRuntime::TryRetireAll(FArdaDependencyGraph::FArdaImpl& Graph)
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

}
