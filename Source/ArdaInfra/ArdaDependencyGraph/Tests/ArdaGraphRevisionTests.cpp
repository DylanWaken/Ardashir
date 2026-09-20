#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "ArdaTestBackend.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	FArdaRHIBufferDesc MakeRevisionBufferDesc(uint64_t ByteSize = 16)
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = ByteSize;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		return Desc;
	}

	FArdaRHIStatus QueueRevisionUpload(FArdaDependencyGraph& Graph,
	    uint8_t Value,
	    uint64_t ByteSize = 16,
	    FArdaGraphBuffer* Resource = nullptr,
	    const eastl::string& Name = {},
	    const FArdaInductorOptions* Options = nullptr)
	{
		FArdaGraphEdit Edit(Graph);
		if (Options)
		{
			Edit.SetOptions(*Options);
		}
		const auto Buffer = AllocateBuffer(Edit, MakeRevisionBufferDesc(ByteSize), Name);
		Upload(Edit, Buffer, eastl::vector<uint8_t>(static_cast<size_t>(ByteSize), Value));
		Edit.Keep(Buffer);
		if (Resource)
		{
			*Resource = Buffer;
		}
		return Edit.Commit();
	}

	void UnlockRevisionWithoutDevice(FArdaDependencyGraph& Graph)
	{
		// A submission attempt consumes the compile/edit lock even when no device can execute it.
		EXPECT_FALSE(Graph.Execute().mStatus);
		EXPECT_FALSE(Graph.GetRevisionStats().mbEditQueueLocked);
	}

	TEST(ArdaGraphRevision, ClosingEditsQueuesConstructionWithoutCompiling)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7));
		const auto Queued = Graph.GetRevisionStats();
		EXPECT_EQ(Queued.mPendingEditCount, 1u);
		EXPECT_EQ(Queued.mCompileCount, 0u);
		EXPECT_EQ(Queued.mCachedRevisionCount, 0u);
		EXPECT_FALSE(Queued.mbEditQueueLocked);
		EXPECT_TRUE(Graph.GetCompileResult().mExecutionOrder.empty());
		EXPECT_FALSE(Graph.Execute().mStatus);
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
		ASSERT_TRUE(Graph.Compile());
		const auto Compiled = Graph.GetRevisionStats();
		EXPECT_EQ(Compiled.mPendingEditCount, 0u);
		EXPECT_EQ(Compiled.mCompileCount, 1u);
		EXPECT_EQ(Compiled.mCachedRevisionCount, 1u);
		EXPECT_TRUE(Compiled.mbEditQueueLocked);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 1u);
	}

	TEST(ArdaGraphRevision, CompileLocksEditsUntilAnExecutionAttempt)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7));
		ASSERT_TRUE(Graph.Compile());
		const auto Revision = Graph.GetCompileResult().mRevision;
		{
			FArdaGraphEdit Rejected(Graph);
			EXPECT_FALSE(Rejected.IsOpen());
			EXPECT_FALSE(Rejected.Commit());
		}
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
		UnlockRevisionWithoutDevice(Graph);
		FArdaGraphEdit Fresh(Graph);
		ASSERT_TRUE(Fresh.IsOpen());
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		EXPECT_FALSE(Graph.Compile());
		EXPECT_FALSE(Graph.Execute().mStatus);
		EXPECT_TRUE(Fresh.IsOpen());
		ASSERT_TRUE(Fresh.Cancel());
	}

	TEST(ArdaGraphRevision, MultipleEditsAccumulateAndRetainCrossEditDependencies)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphBuffer Source;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 31, 16, &Source));
		const auto FirstHash = Graph.GetRevisionStats().mConstructionHash;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Destination = Clone(Edit, Source);
			ASSERT_TRUE(Destination);
			Edit.Keep(Destination);
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 2u);
		EXPECT_NE(Graph.GetRevisionStats().mConstructionHash, FirstHash);
		const auto Attached = Graph.GetTopology().GetNodes();
		ASSERT_EQ(Attached.size(), 2u);
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, Attached);
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 0u);
	}

	TEST(ArdaGraphRevision, RepeatingFacadeConstructionReusesCompilationDespiteFreshHandles)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphBuffer First;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7, 16, &First));
		ASSERT_TRUE(Graph.Compile());
		const auto Revision = Graph.GetCompileResult().mRevision;
		const auto ConstructionHash = Graph.GetRevisionStats().mConstructionHash;
		const auto Schedule = Graph.GetCompileResult().mExecutionOrder;
		UnlockRevisionWithoutDevice(Graph);
		FArdaGraphBuffer Second;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7, 16, &Second));
		EXPECT_NE(First.GetHandle(), Second.GetHandle());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, Schedule);
		const auto Stats = Graph.GetRevisionStats();
		EXPECT_EQ(Stats.mConstructionHash, ConstructionHash);
		EXPECT_EQ(Stats.mPersistentRevision, Revision);
		EXPECT_EQ(Stats.mCompileCount, 1u);
		EXPECT_EQ(Stats.mCacheHitCount, 1u);
		EXPECT_TRUE(Stats.mbEditQueueLocked);
	}

	TEST(ArdaGraphRevision, AlternatingConstructionsRetainAndReuseBothRevisions)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 11));
		ASSERT_TRUE(Graph.Compile());
		const auto FirstRevision = Graph.GetCompileResult().mRevision;
		const auto FirstHash = Graph.GetRevisionStats().mConstructionHash;
		UnlockRevisionWithoutDevice(Graph);
		ASSERT_TRUE(QueueRevisionUpload(Graph, 23));
		ASSERT_TRUE(Graph.Compile());
		EXPECT_NE(Graph.GetCompileResult().mRevision, FirstRevision);
		EXPECT_NE(Graph.GetRevisionStats().mConstructionHash, FirstHash);
		UnlockRevisionWithoutDevice(Graph);
		ASSERT_TRUE(QueueRevisionUpload(Graph, 11));
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, FirstRevision);
		const auto Stats = Graph.GetRevisionStats();
		EXPECT_EQ(Stats.mConstructionHash, FirstHash);
		EXPECT_EQ(Stats.mCompileCount, 2u);
		EXPECT_EQ(Stats.mCacheHitCount, 1u);
		EXPECT_EQ(Stats.mCachedRevisionCount, 2u);
	}

	TEST(ArdaGraphRevision, DescriptorsExplicitNamesAndCompilerOptionsAffectConstructionIdentity)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7, 16, nullptr, "first"));
		ASSERT_TRUE(Graph.Compile());
		const auto OriginalHash = Graph.GetRevisionStats().mConstructionHash;
		UnlockRevisionWithoutDevice(Graph);
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7, 32, nullptr, "first"));
		ASSERT_TRUE(Graph.Compile());
		EXPECT_NE(Graph.GetRevisionStats().mConstructionHash, OriginalHash);
		UnlockRevisionWithoutDevice(Graph);
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7, 16, nullptr, "renamed"));
		ASSERT_TRUE(Graph.Compile());
		EXPECT_NE(Graph.GetRevisionStats().mConstructionHash, OriginalHash);
		UnlockRevisionWithoutDevice(Graph);
		auto Options = Graph.GetOptions();
		Options.mObjective = EArdaInductorObjective::Memory;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7, 16, nullptr, "first", &Options));
		ASSERT_TRUE(Graph.Compile());
		EXPECT_NE(Graph.GetRevisionStats().mConstructionHash, OriginalHash);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 4u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 0u);
	}

	TEST(ArdaGraphRevision, OperationAndEditOrderingArePartOfConstructionIdentity)
	{
		FArdaDependencyGraph Graph;
		uint64_t FirstHash = 0;
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			FArdaGraphBuffer Buffer;
			{
				FArdaGraphEdit Edit(Graph);
				Buffer = AllocateBuffer(Edit, MakeRevisionBufferDesc());
				Upload(Edit, Buffer, eastl::vector<uint8_t>(16, Frame == 0 ? 3 : 5));
				Edit.Keep(Buffer);
				ASSERT_TRUE(Edit.Commit());
			}
			{
				FArdaGraphEdit Edit(Graph);
				Upload(Edit, Buffer, eastl::vector<uint8_t>(16, Frame == 0 ? 5 : 3));
				ASSERT_TRUE(Edit.Commit());
			}
			ASSERT_TRUE(Graph.Compile());
			if (Frame == 0)
			{
				FirstHash = Graph.GetRevisionStats().mConstructionHash;
			}
			else
			{
				EXPECT_NE(Graph.GetRevisionStats().mConstructionHash, FirstHash);
			}
			UnlockRevisionWithoutDevice(Graph);
		}
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 0u);
	}

	TEST(ArdaGraphRevision, OldResourceHandlesCannotEnterAFreshConstruction)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphBuffer Old;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7, 16, &Old));
		ASSERT_TRUE(Graph.Compile());
		UnlockRevisionWithoutDevice(Graph);
		FArdaGraphEdit Edit(Graph);
		const auto Fresh = AllocateBuffer(Edit, MakeRevisionBufferDesc());
		ASSERT_TRUE(Fresh);
		EXPECT_NE(Fresh.GetHandle(), Old.GetHandle());
		EXPECT_EQ(Graph.FindResource(Old.GetHandle()), nullptr);
		EXPECT_FALSE(Edit.UseBuffer(Old.GetHandle()));
		EXPECT_FALSE(Edit.Commit());
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 0u);
	}

	TEST(ArdaGraphRevision, FailedCompileRetainsQueueAndAdditionalEditsCanRepairIt)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphBuffer Uninitialized;
		{
			FArdaGraphEdit Edit(Graph);
			Uninitialized = AllocateBuffer(Edit, MakeRevisionBufferDesc());
			ASSERT_TRUE(Readback(Edit, Uninitialized));
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_FALSE(Graph.Compile());
		EXPECT_FALSE(Graph.IsEditing());
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
		EXPECT_FALSE(Graph.GetRevisionStats().mbEditQueueLocked);
		EXPECT_FALSE(Graph.Execute().mStatus);
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
		{
			FArdaGraphEdit Repair(Graph);
			ASSERT_TRUE(Upload(Repair, Uninitialized, eastl::vector<uint8_t>(16, 13)));
			ASSERT_TRUE(Repair.Commit());
		}
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 2u);
		const auto Repaired = Graph.Compile();
		ASSERT_TRUE(Repaired) << Repaired.mMessage.c_str();
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 0u);
		EXPECT_TRUE(Graph.GetRevisionStats().mbEditQueueLocked);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
	}

	TEST(ArdaGraphRevision, DiscardingFailedConstructionRestoresThePersistentRevision)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphBuffer Persistent;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7, 16, &Persistent));
		ASSERT_TRUE(Graph.Compile());
		const auto Revision = Graph.GetCompileResult().mRevision;
		const auto Schedule = Graph.GetCompileResult().mExecutionOrder;
		UnlockRevisionWithoutDevice(Graph);
		FArdaGraphBuffer Invalid;
		{
			FArdaGraphEdit Edit(Graph);
			Invalid = AllocateBuffer(Edit, MakeRevisionBufferDesc());
			ASSERT_TRUE(Readback(Edit, Invalid));
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_FALSE(Graph.Compile());
		ASSERT_TRUE(Graph.DiscardPendingEdits());
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 0u);
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, Schedule);
		EXPECT_EQ(Graph.GetTopology().GetNodes(), Schedule);
		EXPECT_NE(Graph.FindResource(Persistent.GetHandle()), nullptr);
		EXPECT_EQ(Graph.FindResource(Invalid.GetHandle()), nullptr);
		UnlockRevisionWithoutDevice(Graph);
	}

	TEST(ArdaGraphRevision, CancelingOneEditPreservesEarlierQueuedEdits)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphBuffer Buffer;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7, 16, &Buffer));
		const auto Hash = Graph.GetRevisionStats().mConstructionHash;
		{
			FArdaGraphEdit Abandoned(Graph);
			ASSERT_TRUE(Clone(Abandoned, Buffer));
		}
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
		EXPECT_EQ(Graph.GetRevisionStats().mConstructionHash, Hash);
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 1u);
	}

	TEST(ArdaGraphRevision, ForceCompileAndManualInvalidationBypassAnIdenticalCachedConstruction)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7));
		ASSERT_TRUE(Graph.Compile());
		const auto FirstRevision = Graph.GetCompileResult().mRevision;
		UnlockRevisionWithoutDevice(Graph);
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7));
		ASSERT_TRUE(Graph.Compile(true));
		EXPECT_NE(Graph.GetCompileResult().mRevision, FirstRevision);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 0u);
		UnlockRevisionWithoutDevice(Graph);
		const auto ForcedRevision = Graph.GetCompileResult().mRevision;
		ASSERT_TRUE(Graph.InvalidatePersistentCache());
		EXPECT_EQ(Graph.GetRevisionStats().mCachedRevisionCount, 0u);
		EXPECT_EQ(Graph.GetCompileResult().mRevision, ForcedRevision);
		ASSERT_TRUE(QueueRevisionUpload(Graph, 7));
		ASSERT_TRUE(Graph.Compile());
		EXPECT_NE(Graph.GetCompileResult().mRevision, ForcedRevision);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 3u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 0u);
	}

	TEST(ArdaGraphRevision, BoundedRevisionCacheEvictsOlderConstructions)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.SetRevisionCacheCapacity(1));
		for (const uint8_t Value : {11, 23, 11})
		{
			ASSERT_TRUE(QueueRevisionUpload(Graph, Value));
			ASSERT_TRUE(Graph.Compile());
			EXPECT_EQ(Graph.GetRevisionStats().mCachedRevisionCount, 1u);
			UnlockRevisionWithoutDevice(Graph);
		}
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 3u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 0u);
	}

	class FArdaGraphRevisionGpu : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			if (!FindBackendModule(GetParam()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mMessageCallback = &mDiagnostics;
			ASSERT_TRUE(ConfigureBackend(Configuration)) << GetBackendError().c_str();
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
		}

		void TearDown() override
		{
			const bool bMissingValidation = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice->RunGarbageCollection();
				mDevice.Reset();
			}
			ShutdownBackend();
			if (!bMissingValidation)
			{
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}

		FArdaRHIStatus QueueReadbackRevision(FArdaDependencyGraph& Graph,
		    uint8_t Value,
		    const eastl::shared_ptr<eastl::vector<uint8_t>>& Destination)
		{
			FArdaGraphEdit Edit(Graph);
			const auto Source = AllocateBuffer(Edit, MakeRevisionBufferDesc());
			Upload(Edit, Source, eastl::vector<uint8_t>(16, Value));
			const auto Copy = Clone(Edit, Source);
			ReadbackBuffer(Edit, {Copy, Destination});
			return Edit.Commit();
		}

		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(FArdaGraphRevisionGpu, AlternatingRevisionsReuseCompiledWorkAndKeepOldFrameTicketsValid)
	{
		FArdaDependencyGraph Graph(mDevice);
		auto FirstBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		auto SecondBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(QueueReadbackRevision(Graph, 17, FirstBytes));
		ASSERT_TRUE(Graph.Compile());
		const auto FirstRevision = Graph.GetCompileResult().mRevision;
		const auto First = Graph.Submit();
		ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
		EXPECT_FALSE(Graph.GetRevisionStats().mbEditQueueLocked);
		ASSERT_TRUE(QueueReadbackRevision(Graph, 29, SecondBytes));
		ASSERT_TRUE(Graph.Compile());
		const auto Second = Graph.Submit();
		ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
		const auto FirstCompleted = Graph.Wait(First.mValue);
		ASSERT_TRUE(FirstCompleted.mStatus) << FirstCompleted.mStatus.mMessage.c_str();
		EXPECT_EQ(*FirstBytes, eastl::vector<uint8_t>(16, 17));
		const auto SecondCompleted = Graph.Wait(Second.mValue);
		ASSERT_TRUE(SecondCompleted.mStatus) << SecondCompleted.mStatus.mMessage.c_str();
		EXPECT_EQ(*SecondBytes, eastl::vector<uint8_t>(16, 29));
		ASSERT_TRUE(QueueReadbackRevision(Graph, 17, FirstBytes));
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, FirstRevision);
		FirstBytes->clear();
		const auto Reused = Graph.Execute();
		ASSERT_TRUE(Reused.mStatus) << Reused.mStatus.mMessage.c_str();
		EXPECT_EQ(*FirstBytes, eastl::vector<uint8_t>(16, 17));
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 1u);
		EXPECT_TRUE(Graph.IsComplete(First.mValue).mValue);
		EXPECT_TRUE(Graph.IsComplete(Second.mValue).mValue);
		EXPECT_TRUE(Graph.Wait(First.mValue).mStatus);
		EXPECT_TRUE(Graph.Wait(Second.mValue).mStatus);
		// With no new construction queued, the current persistent revision can be replayed directly.
		FirstBytes->clear();
		ASSERT_TRUE(Graph.Execute().mStatus);
		EXPECT_EQ(*FirstBytes, eastl::vector<uint8_t>(16, 17));
	}

	TEST_P(FArdaGraphRevisionGpu, AChangedReadbackDestinationRequiresANewPersistentRevision)
	{
		FArdaDependencyGraph Graph(mDevice);
		auto FirstBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		auto SecondBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(QueueReadbackRevision(Graph, 17, FirstBytes));
		ASSERT_TRUE(Graph.Compile());
		ASSERT_TRUE(Graph.Execute().mStatus);
		ASSERT_TRUE(QueueReadbackRevision(Graph, 17, SecondBytes));
		ASSERT_TRUE(Graph.Compile());
		ASSERT_TRUE(Graph.Execute().mStatus);
		EXPECT_EQ(*FirstBytes, eastl::vector<uint8_t>(16, 17));
		EXPECT_EQ(*SecondBytes, eastl::vector<uint8_t>(16, 17));
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 0u);
	}

	TEST_P(FArdaGraphRevisionGpu, ImportedNativeStorageIsPartOfTheRevisionIdentity)
	{
		const auto FirstNative = mDevice->CreateBuffer(MakeRevisionBufferDesc());
		const auto SecondNative = mDevice->CreateBuffer(MakeRevisionBufferDesc());
		ASSERT_TRUE(FirstNative);
		ASSERT_TRUE(SecondNative);
		FArdaDependencyGraph Graph(mDevice);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		uint64_t FirstRevision = 0;
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			{
				FArdaGraphEdit Edit(Graph);
				const auto Buffer = BindBuffer(Edit, Frame == 1 ? SecondNative.mValue : FirstNative.mValue);
				Upload(Edit, Buffer, eastl::vector<uint8_t>(16, 41));
				ReadbackBuffer(Edit, {Buffer, Bytes});
				ASSERT_TRUE(Edit.Commit());
			}
			ASSERT_TRUE(Graph.Compile());
			if (Frame == 0)
			{
				FirstRevision = Graph.GetCompileResult().mRevision;
			}
			else if (Frame == 1)
			{
				EXPECT_NE(Graph.GetCompileResult().mRevision, FirstRevision);
			}
			else
			{
				EXPECT_EQ(Graph.GetCompileResult().mRevision, FirstRevision);
			}
			Bytes->clear();
			const auto Execution = Graph.Execute();
			ASSERT_TRUE(Execution.mStatus) << Execution.mStatus.mMessage.c_str();
			EXPECT_EQ(*Bytes, eastl::vector<uint8_t>(16, 41));
		}
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 1u);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaGraphRevisionGpu, testing::Values("native-d3d12", "native-vulkan"));
}
