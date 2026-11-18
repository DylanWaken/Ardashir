#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "Inductor/Compiler/ArdaInductorCommandProgramInternal.h"
#include "Inductor/Runtime/ArdaInductorRuntimeStatus.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"
#include "Inductor/Runtime/ArdaInductorCommandExecutor.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <EASTL/unordered_set.h>
#include <cstring>

namespace arda
{
	FArdaGraphExecutionResult FArdaInductorRuntime::WaitState(FArdaDependencyFrameTicket::FArdaState& State)
	{
		std::lock_guard<std::mutex> Lock(State.mMutex);
		if (State.mbComplete)
		{
			return State.mExecution;
		}
		for (const auto Instance : State.mExecution.mLastSubmittedInstances)
		{
			if (!Instance)
			{
				continue;
			}
			const auto Status = State.mDevice->WaitForSubmission(Instance);
			if (!Status && State.mRetirementStatus)
			{
				State.mRetirementStatus = Status;
			}
		}
		for (const auto& Fence : State.mRecoveryFences)
		{
			const auto Status = State.mDevice->WaitGpuFence(Fence);
			if (!Status && State.mRetirementStatus)
			{
				State.mRetirementStatus = Status;
			}
		}
		if (State.mExecution.mStatus && !State.mRetirementStatus)
		{
			State.mExecution.mStatus = State.mRetirementStatus;
		}
		CollectStateTelemetry(State);

		// Only the native completion callback owns each pending promise. Unsubmitted
		// command lists were destroyed before Submit returned, so their waits cancel.
		State.mExecution.mStatus = State.mReadbackCompletions.Drain(State.mExecution.mStatus);
		if (!State.mExecution.mStatus)
		{
			State.mTimers.clear();
			State.mCudaTimers.clear();
		}
		State.mRecoveryFences.clear();
		State.mbComplete = true;
		return State.mExecution;
	}

	FArdaInductorRuntime::~FArdaInductorRuntime()
	{
		(void)WaitAll();
	}

	FArdaRHIStatus FArdaInductorRuntime::WaitAll()
	{
		FArdaRHIStatus Status;
		eastl::vector<eastl::shared_ptr<FArdaDependencyFrameTicket::FArdaState>> Pending;
		for (const auto& Frame : mFrames)
		{
			if (Frame->mActive)
			{
				Pending.push_back(Frame->mActive);
			}
		}
		// Pool wraparound changes slot order. Automatic retirement still publishes
		// pending readbacks in frame order when several frames share a destination.
		eastl::sort(Pending.begin(),
		    Pending.end(),
		    [](const auto& A, const auto& B)
		    {
			    return A->mSequence < B->mSequence;
		    });
		for (const auto& State : Pending)
		{
			(void)WaitState(*State);
			// A recording/readback error must not prevent editing to repair the graph.
			if (Status && !State->mRetirementStatus)
			{
				Status = State->mRetirementStatus;
			}
		}
		return Status;
	}

	TArdaRHIResult<FArdaDependencyFrameTicket> FArdaInductorRuntime::Submit(FArdaDependencyGraph::FArdaImpl& Graph,
	    const FArdaGraphExecuteOptions& Options)
	{
		if (Graph.mbEditing || Graph.mbExecuting)
		{
			return {{}, MakeArdaInductorRuntimeError("Submit requires a successfully compiled graph outside an edit.")};
		}
		if (!Graph.mCompile.mExecution->mRuntime && Graph.mCompile.mMemoryPlan)
		{
			if (auto Status = MaterializePlan(Graph); !Status)
			{
				return {{}, Status};
			}
		}
		if (!Graph.mCompile.mExecution->mRuntime || Graph.mCompile.mExecution->mRuntime->mFrames.empty())
		{
			return {{}, MakeArdaInductorRuntimeError("Submit requires a successfully compiled graph outside an edit.")};
		}

		AdvanceArdaInductorTelemetry(Graph);

		struct FArdaExecutionGuard
		{
			bool& mExecuting;

			explicit FArdaExecutionGuard(bool& Executing)
			    : mExecuting(Executing)
			{
				mExecuting = true;
			}

			~FArdaExecutionGuard()
			{
				mExecuting = false;
			}
		} Guard(Graph.mbExecuting);

		auto& Runtime = *Graph.mCompile.mExecution->mRuntime;
		auto& Frame = *Runtime.mFrames[Runtime.mNextSlot];
		if (Frame.mActive)
		{
			const auto Previous = WaitState(*Frame.mActive);
			if (!Previous.mStatus)
			{
				return {{}, Previous.mStatus};
			}
		}
		if (Runtime.mPrevious && !Runtime.mPrevious->mExecution.mStatus)
		{
			return {{}, MakeArdaInductorRuntimeError("Repair or recompile the graph after a failed frame submission.")};
		}
		auto State = eastl::make_shared<FArdaDependencyFrameTicket::FArdaState>();
		State->mDevice = Graph.mCompile.mExecution->mDevice;
		State->mGraphIdentity = Graph.mIdentity;
		State->mSequence = Graph.mCompile.mExecution->mNextFrameSequence++;
		State->mTiming = Graph.mCompile.mExecution->mTiming;
		State->mbSampleTiming =
		    Graph.mOptions.mbEnableGpuTiming && (State->mSequence - 1) % Graph.mOptions.mGpuTimingSampleInterval == 0;
		if (State->mbSampleTiming && Frame.mTimingState)
		{
			std::unique_lock<std::mutex> Lock(Frame.mTimingState->mMutex, std::try_to_lock);
			State->mbSampleTiming =
			    Lock.owns_lock() && Frame.mTimingState->mTimers.empty() && Frame.mTimingState->mCudaTimers.empty();
		}
		State->mTimingEpoch = State->mbSampleTiming ? Graph.mCompile.mExecution->mTiming->GetEpoch() : 0;
		if (State->mbSampleTiming)
		{
			State->mTimers = Frame.mTimers;
			State->mCudaTimers = Frame.mCudaTimers;
			Frame.mTimingState = State;
		}
		Frame.mRecordedGraphicsDrawCount = 0;
		Frame.mMergedGraphicsDrawCount = 0;
		Frame.mActive = State;
		FArdaDependencyFrameTicket Ticket;
		Ticket.mState = State;
		uint32_t InterFrameWaits = 0;
		try
		{
			for (const auto& Timer : State->mTimers)
			{
				if (!Graph.mCompile.mExecution->mDevice->ResetTimerQuery(Timer.mQuery))
				{
					State->mbSampleTiming = false;
					break;
				}
			}
			if (!State->mbSampleTiming)
			{
				State->mTimers.clear();
				State->mCudaTimers.clear();
			}
			// A shared object's entry/exit state also carries ownership. Even shared
			// reads are serialized conservatively across frames when queues can differ.
			if (State->mExecution.mStatus && Runtime.mbHasSharedResources && Runtime.mPrevious)
			{
				eastl::array<bool, ArdaRHIQueueTypeCount> UsedQueues{};
				UsedQueues[GetArdaRHIQueueIndex(EArdaRHIQueueType::Graphics)] = true;
				for (const auto Queue : Graph.mCompile.mQueues)
				{
					UsedQueues[GetArdaRHIQueueIndex(Queue)] = true;
				}
				for (uint32_t Consumer = 0; Consumer < UsedQueues.size() && State->mExecution.mStatus; ++Consumer)
				{
					if (!UsedQueues[Consumer])
					{
						continue;
					}
					for (uint32_t Producer = 0; Producer < ArdaRHIQueueTypeCount; ++Producer)
					{
						const auto Instance = Runtime.mPrevious->mExecution.mLastSubmittedInstances[Producer];
						if (!Instance || Producer == Consumer)
						{
							continue;
						}
						State->mExecution.mStatus =
						    Graph.mCompile.mExecution->mDevice->QueueWait(static_cast<EArdaRHIQueueType>(Consumer),
						        static_cast<EArdaRHIQueueType>(Producer),
						        Instance);
						if (!State->mExecution.mStatus)
						{
							break;
						}
						++InterFrameWaits;
					}
				}
			}
			if (State->mExecution.mStatus)
			{
				State->mExecution = FArdaInductorCommandExecutor::Submit(*Frame.mLowered, Options);
			}
		}
		catch (const std::exception& Error)
		{
			State->mExecution = Frame.mLowered->mImpl->mExecutionResult;
			State->mExecution.mStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
		}
		catch (...)
		{
			State->mExecution = Frame.mLowered->mImpl->mExecutionResult;
			State->mExecution.mStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			    "Graph recording or submission threw an exception.");
		}
		State->mReadbackCompletions.SetSubmissions(Frame.mLowered->mImpl->mPassSubmissions, State->mDevice);
		State->mExecution.mQueueWaitCount += InterFrameWaits;
		State->mExecution.mRecordedGraphicsDrawCount = Frame.mRecordedGraphicsDrawCount.load();
		State->mExecution.mMergedGraphicsDrawCount = Frame.mMergedGraphicsDrawCount.load();
		// If a provider throws after accepting work without returning a token, mark
		// only that queue's current tail. Never turn graph retirement into device idle.
		if (Frame.mLowered->mImpl->mSubmittingQueue >= 0)
		{
			auto Fence = Graph.mCompile.mExecution->mDevice->CreateGpuFence();
			if (Fence)
			{
				const auto Status = Graph.mCompile.mExecution->mDevice->SignalGpuFence(Fence.mValue,
				    static_cast<EArdaRHIQueueType>(Frame.mLowered->mImpl->mSubmittingQueue));
				if (Status)
				{
					State->mRecoveryFences.push_back(eastl::move(Fence.mValue));
				}
				else
				{
					State->mRetirementStatus = Status;
				}
			}
			else
			{
				State->mRetirementStatus = Fence.mStatus;
			}
		}
		Runtime.mPrevious = State;
		Runtime.mNextSlot = (Runtime.mNextSlot + 1) % Runtime.mFrames.size();
		return {eastl::move(Ticket), State->mExecution.mStatus};
	}

	FArdaGraphExecutionResult FArdaInductorRuntime::Wait(FArdaDependencyGraph::FArdaImpl& Graph,
	    const FArdaDependencyFrameTicket& Ticket)
	{
		if (Graph.mbExecuting)
		{
			FArdaGraphExecutionResult Result;
			Result.mStatus =
			    MakeArdaInductorRuntimeError("Wait requires this graph's frame ticket outside a callback.");
			return Result;
		}
		return Wait(Graph.mIdentity, Ticket);
	}

	FArdaGraphExecutionResult FArdaInductorRuntime::Wait(uint64_t GraphIdentity,
	    const FArdaDependencyFrameTicket& Ticket)
	{
		if (!Ticket.mState || Ticket.mState->mGraphIdentity != GraphIdentity)
		{
			FArdaGraphExecutionResult Result;
			Result.mStatus =
			    MakeArdaInductorRuntimeError("Wait requires this graph's frame ticket outside a callback.");
			return Result;
		}
		return WaitState(*Ticket.mState);
	}

	TArdaRHIResult<bool> FArdaInductorRuntime::IsComplete(FArdaDependencyGraph::FArdaImpl& Graph,
	    const FArdaDependencyFrameTicket& Ticket)
	{
		if (Graph.mbExecuting)
		{
			return {false,
			    MakeArdaInductorRuntimeError("IsComplete requires this graph's frame ticket outside a callback.")};
		}
		return IsComplete(Graph.mIdentity, Ticket);
	}

	TArdaRHIResult<bool> FArdaInductorRuntime::IsComplete(uint64_t GraphIdentity,
	    const FArdaDependencyFrameTicket& Ticket)
	{
		if (!Ticket.mState || Ticket.mState->mGraphIdentity != GraphIdentity)
		{
			return {false,
			    MakeArdaInductorRuntimeError("IsComplete requires this graph's frame ticket outside a callback.")};
		}

		auto& State = *Ticket.mState;
		std::unique_lock<std::mutex> Lock(State.mMutex, std::try_to_lock);
		if (!Lock.owns_lock())
		{
			return {false, {}};
		}
		if (State.mbComplete)
		{
			return {true, {}};
		}
		for (const auto Instance : State.mExecution.mLastSubmittedInstances)
		{
			if (!Instance)
			{
				continue;
			}
			const auto Ready = State.mDevice->PollSubmission(Instance);
			if (!Ready || !Ready.mValue)
			{
				return Ready;
			}
		}
		for (const auto& Fence : State.mRecoveryFences)
		{
			const auto Ready = State.mDevice->PollGpuFence(Fence);
			if (!Ready || !Ready.mValue)
			{
				return Ready;
			}
		}
		return {State.mReadbackCompletions.IsReady(), {}};
	}
}
