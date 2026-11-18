#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Inductor/Runtime/ArdaInductorCommandExecutor.h"
#include "Inductor/Runtime/ArdaInductorCommandRecording.h"
#include "ArdaTrace.h"

#include <EASTL/algorithm.h>

namespace arda
{
	namespace
	{
		struct FArdaInductorRecordingFailureGuard
		{
			/** Non-owning graph implementation protected for this execution attempt. */
			FArdaInductorCommandProgram::FArdaImpl& mGraph;
			/** Disarmed only after every command has been accepted. */
			bool mbCompleted = false;

			/**
             * Makes an interrupted frame slot terminal until the graph is repaired.
             *
             * Submission disarms the guard only after all commands are accepted,
             * so exceptions cannot leave a
             * partially submitted graph looking reusable.
             */
			~FArdaInductorRecordingFailureGuard()
			{
				if (!mbCompleted)
				{
					mGraph.mbFailed = true;
				}
			}
		};

	}

	const FArdaGraphExecutionResult& FArdaInductorCommandExecutor::Submit(FArdaInductorCommandProgram& Program,
	    const FArdaGraphExecuteOptions& Options)
	{
		auto& Graph = *Program.mImpl;
		Graph.mPassSubmissions.clear();
		if (!Graph.mbCompiled || !Graph.mDevice || Graph.mbFailed || !Graph.mActivePassAccess.empty() ||
		    (Graph.mbExecutionStarted && !Graph.mbExecuted))
		{
			Graph.mExecutionResult.mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Inductor submission requires a finalized, retired, successful command program.");
			return Graph.mExecutionResult;
		}
		Graph.mbExecutionStarted = true;
		Graph.mbExecuted = false;
		Graph.mSubmittingQueue = -1;
		FArdaInductorRecordingFailureGuard FailureGuard{Graph};

		Graph.mExecutionResult = {};
		Graph.mExecutionResult.mQueueDependencies = Graph.mPlan.mQueueDependencies;
		auto RuntimeTransitions = BuildPhysicalTransitions(Graph);
		for (const auto& Alias : Graph.mReusableAliasActivations)
		{
			RuntimeTransitions[Alias.mConsumer.GetIndex()].mAliasingResources.push_back(
			    {Alias.mType, Alias.mResourceIndex, Alias.mType == EArdaGraphResourceType::Texture});
			++Graph.mExecutionResult.mAliasingBarrierCount;
		}

		eastl::vector<FArdaInductorRecordedCommand> Recorded(Graph.mPasses.GetCount());
		const auto Record = [&](FArdaInductorCommandHandle Handle)
		{
			const auto Index = Handle.GetIndex();
			Recorded[Index] =
			    RecordPass(Program, Graph, Handle, RuntimeTransitions[Index], Options.mbValidateResourceStates);
		};

		for (const auto Handle : Graph.mPlan.mExecutionOrder)
		{
			const auto& Pass = Graph.mPasses.Get(Handle);
			if (Pass.mbRecordAtSubmit ||
			    (Pass.GetState().mbSentinel && RuntimeTransitions[Handle.GetIndex()].IsEmpty()))
			{
				continue;
			}
			Record(Handle);
			if (!Recorded[Handle.GetIndex()].mStatus)
			{
				Graph.mExecutionResult.mStatus = Recorded[Handle.GetIndex()].mStatus;
				return Graph.mExecutionResult;
			}
		}
		Graph.mPassSubmissions.resize(Graph.mPasses.GetCount(), 0);

		// Submission tokens order subsequent cross-queue consumers.
		for (FArdaInductorCommandHandle Handle : Graph.mPlan.mExecutionOrder)
		{
			FArdaInductorRecordedCommand& Pass = Recorded[Handle.GetIndex()];
			if (Graph.mPasses.Get(Handle).mbRecordAtSubmit)
			{
				Record(Handle);
				if (!Pass.mStatus)
				{
					Graph.mExecutionResult.mStatus = Pass.mStatus;
					break;
				}
			}
			if (!Pass.mCommandList)
			{
				continue;
			}

			for (FArdaGraphStateConformanceRecord& Record : Pass.mStateConformanceRecords)
			{
				if (!Record.IsConsistent())
				{
					++Graph.mExecutionResult.mStateConformanceFailureCount;
				}
				Graph.mExecutionResult.mStateConformanceRecords.push_back(eastl::move(Record));
			}
			if (Options.mbValidateResourceStates && Graph.mExecutionResult.mStateConformanceFailureCount != 0)
			{
				Graph.mExecutionResult.mStatus = arda::FArdaRHIStatus::Error(arda::EArdaRHIResult::InvalidState,
				    "Inductor resource state differs from the facade/backend/native state.");
				break;
			}

			const size_t ConsumerQueueIndex = arda::GetArdaRHIQueueIndex(Pass.mQueue);
			eastl::array<uint64_t, arda::ArdaRHIQueueTypeCount> RequiredInstances{};

			for (const FArdaGraphQueueDependency& Dependency : Graph.mExecutionResult.mQueueDependencies)
			{
				if (Dependency.mConsumer != Handle.GetIndex())
				{
					continue;
				}
				const uint64_t ProducerInstance = Graph.mPassSubmissions[Dependency.mProducer];
				if (ProducerInstance == 0)
				{
					continue;
				}
				auto& Required = RequiredInstances[arda::GetArdaRHIQueueIndex(Dependency.mProducerQueue)];
				Required = eastl::max(Required, ProducerInstance);
			}

			for (size_t QueueIndex = 0; QueueIndex < RequiredInstances.size(); ++QueueIndex)
			{
				if (!RequiredInstances[QueueIndex])
				{
					continue;
				}
				auto Status = Graph.mDevice->QueueWait(Pass.mQueue,
				    static_cast<arda::EArdaRHIQueueType>(QueueIndex),
				    RequiredInstances[QueueIndex]);
				if (!Status)
				{
					Graph.mExecutionResult.mStatus = eastl::move(Status);
					break;
				}
				++Graph.mExecutionResult.mQueueWaitCount;
			}
			if (!Graph.mExecutionResult.mStatus)
			{
				break;
			}

			Graph.mSubmittingQueue = static_cast<int32_t>(ConsumerQueueIndex);
			const auto SubmitResult = Graph.mDevice->ExecuteCommandList(Pass.mCommandList);
			Graph.mSubmittingQueue = -1;
			if (!SubmitResult)
			{
				Graph.mExecutionResult.mStatus = SubmitResult.mStatus;
				++Graph.mExecutionResult.mSubmissionFailureCount;
				if (SubmitResult.mStatus.mCode == arda::EArdaRHIResult::InvalidState)
				{
					++Graph.mExecutionResult.mStateConformanceFailureCount;
				}
				break;
			}
			const uint64_t Instance = SubmitResult.mValue;
			Graph.mPassSubmissions[Handle.GetIndex()] = Instance;
			Graph.mExecutionResult.mLastSubmittedInstances[ConsumerQueueIndex] = Instance;
			++Graph.mExecutionResult.mSubmittedCommandListCount;
		}

		Graph.mDevice->RunGarbageCollection();
		Graph.mbExecuted = Graph.mExecutionResult.mStatus.IsSuccess();
		FailureGuard.mbCompleted = Graph.mbExecuted;
		ARDA_TRACE_COUNTER("Inductor Submitted Command Lists", Graph.mExecutionResult.mSubmittedCommandListCount);
		ARDA_TRACE_COUNTER("Inductor Queue Waits", Graph.mExecutionResult.mQueueWaitCount);
		return Graph.mExecutionResult;
	}
}
