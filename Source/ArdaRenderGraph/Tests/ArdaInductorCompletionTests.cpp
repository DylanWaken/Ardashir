#include "ArdaDependencyGraphInternal.h"

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <limits>

namespace
{
	using namespace arda;
	using namespace std::chrono_literals;

	TEST(ArdaInductorCompletions, PublishOnlyAfterDrainInCompiledPassOrder)
	{
		FArdaInductorReadbackCompletions Completions;
		auto Destination = eastl::make_shared<eastl::vector<uint8_t>>(eastl::vector<uint8_t>{0});
		// Parallel recording and callback order can both differ from graph order.
		auto LaterPass = Completions.Register(Destination, 9);
		auto EarlierPass = Completions.Register(Destination, 2);
		LaterPass({{9}, {}});
		EarlierPass({{2}, {}});
		EXPECT_EQ(*Destination, (eastl::vector<uint8_t>{0}));
		ASSERT_TRUE(Completions.Drain({}));
		EXPECT_EQ(*Destination, (eastl::vector<uint8_t>{9}));
		auto NextFrame = Completions.Register(Destination, 1);
		NextFrame({{17}, {}});
		ASSERT_TRUE(Completions.Drain({}));
		EXPECT_EQ(*Destination, (eastl::vector<uint8_t>{17}));
	}

	TEST(ArdaInductorCompletions, UnsubmittedRecordedListDestructionCancelsItsWait)
	{
		FArdaInductorReadbackCompletions Completions;
		auto Destination = eastl::make_shared<eastl::vector<uint8_t>>(eastl::vector<uint8_t>{99});
		{
			// The facade's unsubmitted command-list completion array owns this callback.
			eastl::vector<FArdaRHIDeviceToHostCopyCallback> RecordedList;
			RecordedList.push_back(Completions.Register(Destination, 1));
		}
		const auto Failure = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "recording failed");
		const auto Drained = Completions.Drain(Failure);
		EXPECT_EQ(Drained.mCode, Failure.mCode);
		EXPECT_EQ(Drained.mMessage, Failure.mMessage);
		EXPECT_TRUE(Destination->empty());
		EXPECT_TRUE(Completions.Drain({}));
	}

	TEST(ArdaInductorCompletions, PartialSubmissionDrainsOutstandingWorkAndCancelsTheUnsubmittedRemainder)
	{
		FArdaInductorReadbackCompletions Completions;
		auto SubmittedOutput = eastl::make_shared<eastl::vector<uint8_t>>(eastl::vector<uint8_t>{1});
		auto UnsubmittedOutput = eastl::make_shared<eastl::vector<uint8_t>>(eastl::vector<uint8_t>{2});
		std::promise<void> AllowCompletion;
		auto CompletionGate = AllowCompletion.get_future();
		std::thread CallbackThread(
		    [Callback = Completions.Register(SubmittedOutput, 1), Gate = std::move(CompletionGate)]() mutable
		    {
			    Gate.wait();
			    Callback({{7}, {}});
		    });
		{
			auto DiscardedListCallback = Completions.Register(UnsubmittedOutput, 2);
		}
		const auto Failure = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "later submission failed");
		std::promise<void> DrainStarted;
		auto Started = DrainStarted.get_future();
		auto Drain = std::async(std::launch::async,
		    [&]
		    {
			    DrainStarted.set_value();
			    return Completions.Drain(Failure);
		    });
		Started.wait();
		EXPECT_EQ(Drain.wait_for(20ms), std::future_status::timeout);
		// Execute cannot finish while the earlier submitted callback can still run.
		AllowCompletion.set_value();
		CallbackThread.join();
		ASSERT_EQ(Drain.wait_for(1s), std::future_status::ready);
		const auto Drained = Drain.get();
		EXPECT_EQ(Drained.mCode, Failure.mCode);
		EXPECT_EQ(Drained.mMessage, Failure.mMessage);
		EXPECT_TRUE(SubmittedOutput->empty());
		EXPECT_TRUE(UnsubmittedOutput->empty());
	}

	TEST(ArdaInductorCompletions, CompletionFailureInvalidatesAllFrameOutputs)
	{
		FArdaInductorReadbackCompletions Completions;
		auto Good = eastl::make_shared<eastl::vector<uint8_t>>();
		auto Failed = eastl::make_shared<eastl::vector<uint8_t>>();
		auto SuccessfulCallback = Completions.Register(Good, 1);
		auto FailedCallback = Completions.Register(Failed, 2);
		SuccessfulCallback({{3, 4}, {}});
		FailedCallback({{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "readback map failed")});
		const auto Drained = Completions.Drain({});
		EXPECT_EQ(Drained.mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(Drained.mMessage, "readback map failed");
		EXPECT_TRUE(Good->empty());
		EXPECT_TRUE(Failed->empty());
	}

	TEST(ArdaInductorCompletions, MissingCallbackCannotMasqueradeAsSuccessfulReadback)
	{
		FArdaInductorReadbackCompletions Completions;
		auto Destination = eastl::make_shared<eastl::vector<uint8_t>>(eastl::vector<uint8_t>{8});
		{
			auto Discarded = Completions.Register(Destination, 0);
		}
		const auto Drained = Completions.Drain({});
		EXPECT_EQ(Drained.mCode, EArdaRHIResult::InvalidState);
		EXPECT_TRUE(Destination->empty());
	}

	TEST(ArdaInductorCompletions, TimingIdentitySeparatesDisplayNamesGenerationsAndGraphs)
	{
		FArdaInductorTimingAccumulator Profile;
		const FArdaGraphNodeHandle A(1, 1, 100), B(2, 1, 100), C(3, 1, 100);
		const FArdaGraphNodeHandle NewA(1, 2, 100), ForeignA(1, 1, 200);
		Profile.Add("CUDA batch: A + B", 0.001, {A, B});
		Profile.Add("CUDA batch: A + B", 0.007, {C});
		Profile.Add("different display label", 0.003, {A, B});
		Profile.Add("CUDA batch: A + B", 0.011, {NewA, B});
		Profile.Add("CUDA batch: A + B", 0.013, {ForeignA, B});
		Profile.Add("CUDA batch: A + B", 0.017); // Synthetic name-only samples occupy a separate namespace.
		const auto Samples = Profile.Snapshot();
		ASSERT_EQ(Samples.size(), 5u);
		for (const auto& Sample : Samples)
		{
			if (Sample.mNodes == eastl::vector<FArdaGraphNodeHandle>{A, B})
			{
				EXPECT_EQ(Sample.mSampleCount, 2u);
				EXPECT_DOUBLE_EQ(Sample.mGpuSeconds, 0.0014);
				EXPECT_EQ(Sample.mNodeName, "different display label");
			}
			else
			{
				EXPECT_EQ(Sample.mSampleCount, 1u);
			}
		}
	}

	TEST(ArdaInductorCompletions, TimingProfileKeepsFiniteEmaAndResetsExplicitly)
	{
		FArdaInductorTimingAccumulator Profile;
		Profile.Add("later", 0.004);
		Profile.Add("first", 0.001);
		Profile.Add("first", 0.003);
		Profile.Add("invalid", -1);
		Profile.Add("invalid", std::numeric_limits<double>::infinity());
		Profile.Add("invalid", std::numeric_limits<double>::quiet_NaN());
		const auto Samples = Profile.Snapshot();
		ASSERT_EQ(Samples.size(), 2u);
		EXPECT_EQ(Samples[0].mNodeName, "first");
		EXPECT_DOUBLE_EQ(Samples[0].mGpuSeconds, 0.0014);
		EXPECT_EQ(Samples[0].mSampleCount, 2u);
		Profile.Clear();
		EXPECT_TRUE(Profile.Snapshot().empty());
	}

	TEST(ArdaInductorCompletions, TelemetryUsesConfiguredEmaAndBoundsHistoryWithinTheCurrentEpoch)
	{
		FArdaInductorTimingAccumulator Profile;
		Profile.Configure(0.5, 2);
		const FArdaGraphNodeHandle Node(1, 1, 100);
		const auto Epoch = Profile.GetEpoch();
		Profile.Add("node", .001, {Node}, 1, EArdaRHIQueueType::Compute, Epoch);
		Profile.Add("node", .005, {Node}, 2, EArdaRHIQueueType::Compute, Epoch);
		Profile.Add("node", .007, {Node}, 3, EArdaRHIQueueType::Compute, Epoch);
		const auto Samples = Profile.Snapshot();
		ASSERT_EQ(Samples.size(), 1u);
		EXPECT_DOUBLE_EQ(Samples[0].mGpuSeconds, .005);
		EXPECT_EQ(Samples[0].mSampleCount, 3u);
		EXPECT_DOUBLE_EQ(Samples[0].mMinGpuSeconds, .001);
		EXPECT_DOUBLE_EQ(Samples[0].mMaxGpuSeconds, .007);
		const auto History = Profile.History();
		ASSERT_EQ(History.size(), 2u);
		EXPECT_EQ(History[0].mFrameSequence, 2u);
		EXPECT_EQ(History[1].mFrameSequence, 3u);
		EXPECT_EQ(History[1].mQueue, EArdaRHIQueueType::Compute);
		Profile.Clear();
		Profile.Add("old ticket", .5, {Node}, 4, EArdaRHIQueueType::Compute, Epoch);
		EXPECT_TRUE(Profile.History().empty());
		EXPECT_TRUE(Profile.Snapshot().empty());
		Profile.Configure(1, 0);
		Profile.Add("node", .004, {Node}, 5);
		Profile.Add("node", .002, {Node}, 6);
		EXPECT_DOUBLE_EQ(Profile.Snapshot()[0].mGpuSeconds, .002);
		EXPECT_TRUE(Profile.History().empty());
	}

	TEST(ArdaInductorCompletions, TelemetryOptionsValidateCadenceSmoothingAndAdaptivePrerequisites)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		EXPECT_FALSE(Options.mbEnableGpuTiming);
		EXPECT_FALSE(Options.mbEnableAdaptiveScheduling);
		Options.mbEnableAdaptiveScheduling = true;
		EXPECT_FALSE(Graph.SetOptions(Options));
		Options.mbEnableGpuTiming = true;
		EXPECT_TRUE(Graph.SetOptions(Options));
		Options.mGpuTimingEmaAlpha = 0;
		EXPECT_FALSE(Graph.SetOptions(Options));
		Options.mGpuTimingEmaAlpha = std::numeric_limits<double>::quiet_NaN();
		EXPECT_FALSE(Graph.SetOptions(Options));
		Options.mGpuTimingEmaAlpha = 1;
		Options.mGpuTimingSampleInterval = 0;
		EXPECT_FALSE(Graph.SetOptions(Options));
		Options.mGpuTimingSampleInterval = 1;
		Options.mAdaptiveSchedulingMinImprovement = 1;
		EXPECT_FALSE(Graph.SetOptions(Options));
		Options.mAdaptiveSchedulingMinImprovement = 0;
		EXPECT_TRUE(Graph.SetOptions(Options));
		EXPECT_TRUE(Graph.CancelGraphEdit());
	}
}
