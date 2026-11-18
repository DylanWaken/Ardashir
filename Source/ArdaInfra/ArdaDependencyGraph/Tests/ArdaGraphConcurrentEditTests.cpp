#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	using namespace arda;

	/** A bounded rendezvous makes serialization across whole edit scopes fail instead of hanging tests. */
	class FArdaEditRendezvous
	{
	public:
		explicit FArdaEditRendezvous(uint32_t Participants)
		    : mParticipants(Participants)
		{
		}

		bool Arrive()
		{
			std::unique_lock<std::mutex> Lock(mMutex);
			++mArrived;
			mCondition.notify_all();
			return mCondition.wait_for(Lock,
			    std::chrono::seconds(10),
			    [&]
			    {
				    return mArrived == mParticipants;
			    });
		}

	private:
		std::mutex mMutex;
		std::condition_variable mCondition;
		uint32_t mParticipants;
		uint32_t mArrived = 0;
	};

	FArdaRHIBufferDesc ConcurrentBufferDesc()
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16;
		return Desc;
	}

	TEST(ArdaGraphConcurrentEdit, IndependentScopesRecordSimultaneouslyAndPublishEveryOperation)
	{
		constexpr uint32_t ThreadCount = 8;
		constexpr uint32_t Operations = 24;
		FArdaDependencyGraph Graph;
		FArdaEditRendezvous Ready(ThreadCount);
		std::vector<std::thread> Threads;
		std::vector<FArdaRHIStatus> Status(ThreadCount);
		std::vector<FArdaGraphBuffer> Outputs(ThreadCount);
		std::atomic<uint32_t> Recorded{0};
		for (uint32_t Thread = 0; Thread < ThreadCount; ++Thread)
		{
			Threads.emplace_back(
			    [&, Thread]
			    {
				    FArdaGraphEdit Edit(Graph);
				    if (Thread == 0)
				    {
					    auto Options = Edit.GetGraph().GetOptions();
					    Options.mSearchMode = EArdaInductorSearchMode::Greedy;
					    Edit.SetOptions(Options);
				    }
				    for (uint32_t Index = 0; Index < Operations; ++Index)
				    {
					    const auto Buffer = AllocateBuffer(Edit, ConcurrentBufferDesc());
					    Upload(Edit, Buffer, eastl::vector<uint8_t>(16, uint8_t(Thread)));
					    Edit.Keep(Buffer);
					    Outputs[Thread] = Buffer;
				    }
				    if (Edit.GetStatus())
				    {
					    Recorded.fetch_add(1);
				    }
				    EXPECT_TRUE(Ready.Arrive());
				    EXPECT_EQ(Recorded.load(), ThreadCount);
				    Status[Thread] = Edit.Commit();
			    });
		}
		for (auto& Thread : Threads)
		{
			Thread.join();
		}
		for (uint32_t Thread = 0; Thread < ThreadCount; ++Thread)
		{
			ASSERT_TRUE(Status[Thread]) << Status[Thread].mMessage.c_str();
			EXPECT_NE(Graph.FindResource(Outputs[Thread]), nullptr);
		}
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, ThreadCount);
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), ThreadCount * Operations);
		ASSERT_TRUE(Graph.CompileAndWait());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), ThreadCount * Operations);
	}

	TEST(ArdaGraphConcurrentEdit, ConcurrentCommitsKeepFailuresAndCancellationIsolated)
	{
		FArdaDependencyGraph Graph;
		FArdaEditRendezvous Ready(4);
		std::vector<std::thread> Threads;
		std::vector<FArdaRHIStatus> Status(4);
		std::vector<FArdaGraphBuffer> Outputs(4);
		for (uint32_t Index = 0; Index < 4; ++Index)
		{
			Threads.emplace_back(
			    [&, Index]
			    {
				    FArdaGraphEdit Edit(Graph);
				    Outputs[Index] = AllocateBuffer(Edit, ConcurrentBufferDesc());
				    Upload(Edit, Outputs[Index], eastl::vector<uint8_t>(16, uint8_t(Index)));
				    Edit.Keep(Outputs[Index]);
				    if (Index == 2)
				    {
					    Edit.ReportError(
					        FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Intentional authoring failure"));
				    }
				    EXPECT_TRUE(Ready.Arrive());
				    Status[Index] = Index == 3 ? Edit.Cancel() : Edit.Commit();
			    });
		}
		for (auto& Thread : Threads)
		{
			Thread.join();
		}
		EXPECT_TRUE(Status[0]);
		EXPECT_TRUE(Status[1]);
		EXPECT_FALSE(Status[2]);
		EXPECT_TRUE(Status[3]);
		EXPECT_EQ(Graph.FindResource(Outputs[2]), nullptr);
		EXPECT_EQ(Graph.FindResource(Outputs[3]), nullptr);
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 2u);
		ASSERT_TRUE(Graph.CompileAndWait());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
	}

	TEST(ArdaGraphConcurrentEdit, OrderedConcurrentCommitsReuseRevisionsAndPreserveCrossEditHandles)
	{
		constexpr uint32_t ThreadCount = 4;
		FArdaDependencyGraph Graph;
		uint64_t Hash = 0;
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			FArdaEditRendezvous Ready(ThreadCount);
			std::atomic<uint32_t> Turn{0};
			std::vector<std::thread> Threads;
			std::vector<FArdaGraphBuffer> Outputs(ThreadCount);
			std::vector<FArdaRHIStatus> Status(ThreadCount);
			for (uint32_t Index = 0; Index < ThreadCount; ++Index)
			{
				Threads.emplace_back(
				    [&, Index]
				    {
					    FArdaGraphEdit Edit(Graph);
					    Outputs[Index] = AllocateBuffer(Edit, ConcurrentBufferDesc());
					    Upload(Edit, Outputs[Index], eastl::vector<uint8_t>(16, uint8_t(Index)));
					    EXPECT_TRUE(Ready.Arrive());
					    while (Turn.load(std::memory_order_acquire) != Index)
					    {
						    std::this_thread::yield();
					    }
					    Status[Index] = Edit.Commit();
					    Turn.fetch_add(1, std::memory_order_release);
				    });
			}
			for (auto& Thread : Threads)
			{
				Thread.join();
			}
			{
				FArdaGraphEdit Consumer(Graph);
				for (uint32_t Index = 0; Index < ThreadCount; ++Index)
				{
					ASSERT_TRUE(Status[Index]) << Status[Index].mMessage.c_str();
					const auto Copy = Clone(Consumer, Outputs[Index]);
					ASSERT_TRUE(Copy) << Consumer.GetStatus().mMessage.c_str();
					Consumer.Keep(Copy);
				}
				ASSERT_TRUE(Consumer.Commit());
			}
			ASSERT_TRUE(Graph.CompileAndWait());
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), ThreadCount * 2);
			if (Frame == 0)
			{
				Hash = Graph.GetRevisionStats().mConstructionHash;
			}
			else
			{
				EXPECT_EQ(Graph.GetRevisionStats().mConstructionHash, Hash);
				EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 1u);
				EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 1u);
			}
			for (const auto Output : Outputs)
			{
				EXPECT_NE(Graph.FindResource(Output), nullptr);
			}
			EXPECT_FALSE(Graph.ExecuteAndWait().mStatus);
		}
	}

	TEST(ArdaGraphConcurrentEdit, FixedCommitOrderHasTheSameHashForSerialAndOverlappingRecording)
	{
		FArdaDependencyGraph Graph;
		uint64_t Hash = 0;
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			FArdaEditRendezvous Ready(2);
			std::atomic<uint32_t> Turn{0};
			std::vector<std::thread> Threads;
			std::vector<FArdaRHIStatus> Status(2);
			for (uint32_t Index = 0; Index < 2; ++Index)
			{
				Threads.emplace_back(
				    [&, Index]
				    {
					    if (Frame == 1)
					    {
						    while (Turn.load(std::memory_order_acquire) != Index)
						    {
							    std::this_thread::yield();
						    }
					    }
					    FArdaGraphEdit Edit(Graph);
					    const auto Buffer = AllocateBuffer(Edit, ConcurrentBufferDesc());
					    Upload(Edit, Buffer, eastl::vector<uint8_t>(16, uint8_t(Index)));
					    const auto Copy = Clone(Edit, Buffer);
					    Edit.Keep(Copy);
					    if (Frame == 0)
					    {
						    EXPECT_TRUE(Ready.Arrive());
					    }
					    while (Turn.load(std::memory_order_acquire) != Index)
					    {
						    std::this_thread::yield();
					    }
					    Status[Index] = Edit.Commit();
					    Turn.fetch_add(1, std::memory_order_release);
				    });
			}
			for (auto& Thread : Threads)
			{
				Thread.join();
			}
			for (const auto& Result : Status)
			{
				ASSERT_TRUE(Result) << Result.mMessage.c_str();
			}
			ASSERT_TRUE(Graph.CompileAndWait());
			if (Frame == 0)
			{
				Hash = Graph.GetRevisionStats().mConstructionHash;
			}
			else
			{
				EXPECT_EQ(Graph.GetRevisionStats().mConstructionHash, Hash);
				EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 1u);
			}
			EXPECT_FALSE(Graph.ExecuteAndWait().mStatus);
		}
	}

	TEST(ArdaGraphConcurrentEdit, CompileCannotRaceOpenRecordingAndStagedGraphRejectsNewScopes)
	{
		FArdaDependencyGraph Graph;
		FArdaEditRendezvous Open(2);
		FArdaEditRendezvous Checked(2);
		FArdaRHIStatus Committed;
		std::thread Worker(
		    [&]
		    {
			    FArdaGraphEdit Edit(Graph);
			    const auto Buffer = AllocateBuffer(Edit, ConcurrentBufferDesc());
			    Upload(Edit, Buffer, eastl::vector<uint8_t>(16, 7));
			    Edit.Keep(Buffer);
			    EXPECT_TRUE(Open.Arrive());
			    EXPECT_TRUE(Checked.Arrive());
			    Committed = Edit.Commit();
		    });
		EXPECT_TRUE(Open.Arrive());
		EXPECT_TRUE(Graph.IsEditing());
		EXPECT_FALSE(Graph.CompileAndWait());
		EXPECT_FALSE(Graph.DiscardPendingEdits());
		EXPECT_FALSE(Graph.ExecuteAndWait().mStatus);
		EXPECT_TRUE(Checked.Arrive());
		Worker.join();
		ASSERT_TRUE(Committed);
		ASSERT_TRUE(Graph.CompileAndWait());
		std::thread Rejected(
		    [&]
		    {
			    FArdaGraphEdit Edit(Graph);
			    EXPECT_FALSE(Edit.IsOpen());
			    EXPECT_FALSE(Edit.Commit());
		    });
		Rejected.join();
		EXPECT_FALSE(Graph.ExecuteAndWait().mStatus);
		FArdaGraphEdit Next(Graph);
		EXPECT_TRUE(Next.IsOpen());
	}

	TEST(ArdaGraphConcurrentEdit, ConflictingNamesAndOptionsFailAtomically)
	{
		for (bool bOptions : {false, true})
		{
			FArdaDependencyGraph Graph;
			FArdaEditRendezvous Ready(2);
			std::vector<std::thread> Threads;
			std::vector<FArdaRHIStatus> Status(2);
			for (uint32_t Index = 0; Index < 2; ++Index)
			{
				Threads.emplace_back(
				    [&, Index]
				    {
					    FArdaGraphEdit Edit(Graph);
					    const auto Buffer = AllocateBuffer(Edit, ConcurrentBufferDesc(), bOptions ? "" : "shared-name");
					    Upload(Edit, Buffer, eastl::vector<uint8_t>(16, uint8_t(Index)));
					    Edit.Keep(Buffer);
					    if (bOptions)
					    {
						    auto Options = Edit.GetGraph().GetOptions();
						    Options.mFramesInFlight = Index + 1;
						    Edit.SetOptions(Options);
					    }
					    EXPECT_TRUE(Ready.Arrive());
					    Status[Index] = Edit.Commit();
				    });
			}
			for (auto& Thread : Threads)
			{
				Thread.join();
			}
			EXPECT_NE(bool(Status[0]), bool(Status[1]));
			EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
			EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 1u);
			ASSERT_TRUE(Graph.CompileAndWait());
		}
	}
}
