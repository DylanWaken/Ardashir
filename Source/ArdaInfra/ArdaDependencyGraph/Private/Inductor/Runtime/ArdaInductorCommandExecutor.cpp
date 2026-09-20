#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Inductor/Runtime/ArdaInductorCommandExecutor.h"
#include "Inductor/Runtime/ArdaInductorCommandRecording.h"
#include "ArdaTrace.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <future>
#include <thread>

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
		eastl::vector<uint32_t> Levels(Graph.mPasses.GetCount(), 0);
		uint32_t MaxLevel = 0;
		for (FArdaInductorCommandHandle Handle : Graph.mPlan.mExecutionOrder)
		{
			const FArdaInductorCommand& Pass = Graph.mPasses.Get(Handle);
			if (Handle == Graph.mPlan.mPrologue)
			{
				continue;
			}
			uint32_t Level = 0;

			// Fold both data producers and explicit synchronization producers
			// into the earliest safe CPU recording wave for this pass.
			auto AccumulateLevel = [&Graph, &Levels, &Level](FArdaInductorCommandHandle Producer)
			{
				const FArdaInductorCommand* ProducerPass = Graph.mPasses.TryGet(Producer);
				if (ProducerPass != nullptr)
				{
					Level = eastl::max(Level, Levels[Producer.GetIndex()] + 1u);
				}
			};
			for (FArdaInductorCommandHandle Producer : Pass.GetState().mProducers)
			{
				AccumulateLevel(Producer);
			}

			Levels[Handle.GetIndex()] = Level;
			MaxLevel = eastl::max(MaxLevel, Level);
		}

		const auto RecordBatch = [&](FArdaInductorCommandHandle Head)
		{
			Recorded[Head.GetIndex()] =
			    RecordPass(Program, Graph, Head, RuntimeTransitions[Head.GetIndex()], Options.mbValidateResourceStates);
		};

		const uint32_t HardwareThreads = eastl::max(1u, std::thread::hardware_concurrency());
		const uint32_t MaxWorkers =
		    Options.mMaxRecordingThreads == 0 ? HardwareThreads : eastl::max(1u, Options.mMaxRecordingThreads);
		for (uint32_t Level = 0; Level <= MaxLevel; ++Level)
		{
			eastl::vector<FArdaInductorCommandHandle> ParallelPasses;
			eastl::vector<FArdaInductorCommandHandle> SerialPasses;
			for (FArdaInductorCommandHandle Handle : Graph.mPlan.mExecutionOrder)
			{
				if (Levels[Handle.GetIndex()] != Level)
				{
					continue;
				}
				const FArdaInductorCommand& Pass = Graph.mPasses.Get(Handle);
				const bool bHasWork = !Pass.GetState().mbSentinel ||
				    !RuntimeTransitions[Handle.GetIndex()].mTextures.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mBuffers.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mAccelStructs.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mTextureAcquires.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mBufferAcquires.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mTextureReleases.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mBufferReleases.empty() ||
				    !RuntimeTransitions[Handle.GetIndex()].mAliasingResources.empty();
				if (Pass.mbRecordAtSubmit)
				{
					continue;
				}
				if (!bHasWork)
				{
					continue;
				}
				if (Options.mbParallelRecording && !Pass.mbSerialRecord)
				{
					ParallelPasses.push_back(Handle);
				}
				else
				{
					SerialPasses.push_back(Handle);
				}
			}

			for (size_t Begin = 0; Begin < ParallelPasses.size(); Begin += MaxWorkers)
			{
				const size_t End = eastl::min(ParallelPasses.size(), Begin + MaxWorkers);
				eastl::vector<std::future<void>> Futures;
				Futures.reserve(End - Begin);
				Graph.mExecutionResult.mbUsedParallelRecording |= End - Begin > 1;
				for (size_t Index = Begin; Index < End; ++Index)
				{
					const FArdaInductorCommandHandle Handle = ParallelPasses[Index];
					Futures.push_back(std::async(std::launch::async,

					    // Each job owns a distinct command list and writes a
					    // distinct pass-indexed result slot; shared graph data
					    // is read-only during recording except guarded access
					    // validation managed by execution contexts.
					    [&RecordBatch, Handle]
					    {
						    RecordBatch(Handle);
					    }));
				}
				for (auto& Future : Futures)
				{
					Future.get();
				}
			}
			for (FArdaInductorCommandHandle Handle : SerialPasses)
			{
				RecordBatch(Handle);
			}
		}

		// Ordinary recording completes before submission. RecordAtSubmit passes
		// can fail later; report every accepted submission so the frame can retire safely.
		for (const auto& Pass : Recorded)
		{
			if (!Pass.mStatus)
			{
				Graph.mExecutionResult.mStatus = Pass.mStatus;
				return Graph.mExecutionResult;
			}
		}
		eastl::vector<uint64_t> PassInstances(Graph.mPasses.GetCount(), 0);

		// Submission order remains deterministic even when recording completed
		// out of order. Per-pass instances become synchronization tokens for
		// later cross-queue consumers.
		for (FArdaInductorCommandHandle Handle : Graph.mPlan.mExecutionOrder)
		{
			FArdaInductorRecordedCommand& Pass = Recorded[Handle.GetIndex()];
			if (Graph.mPasses.Get(Handle).mbRecordAtSubmit)
			{
				RecordBatch(Handle);
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
				const uint64_t ProducerInstance = PassInstances[Dependency.mProducer];
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
			PassInstances[Handle.GetIndex()] = Instance;
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
