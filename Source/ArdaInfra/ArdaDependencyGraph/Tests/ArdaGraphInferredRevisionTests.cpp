#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "ArdaTestBackend.h"
#include <gtest/gtest.h>

namespace arda::inferred_revision_tests
{
	using namespace arda;

	TEST(ArdaGraphRevision, InferredUploadAndCloneOutputsReuseTheirPersistentRevision)
	{
		FArdaDependencyGraph Graph;
		FArdaDependencyResourceHandle FirstSource;
		FArdaDependencyResourceHandle FirstDestination;
		eastl::string FirstSourceName;
		eastl::string FirstDestinationName;
		uint64_t FirstHash = 0;
		uint64_t FirstRevision = 0;
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.IsOpen());
			const auto UploadOperation = UploadBuffer(Edit, {{}, eastl::vector<uint8_t>(16, uint8_t{37})});
			ASSERT_TRUE(UploadOperation);
			const auto Source = UploadOperation.GetBuffer("Destination");
			const auto Destination = Clone(Edit, Source);
			ASSERT_TRUE(Source);
			ASSERT_TRUE(Destination);
			Edit.Keep(Destination);
			const auto* SourceDesc = Graph.FindResource(Source.GetHandle());
			const auto* DestinationDesc = Graph.FindResource(Destination.GetHandle());
			ASSERT_NE(SourceDesc, nullptr);
			ASSERT_NE(DestinationDesc, nullptr);
			if (!Frame)
			{
				FirstSource = Source.GetHandle();
				FirstDestination = Destination.GetHandle();
				FirstSourceName = SourceDesc->mName;
				FirstDestinationName = DestinationDesc->mName;
			}
			else
			{
				EXPECT_NE(Source.GetHandle(), FirstSource);
				EXPECT_NE(Destination.GetHandle(), FirstDestination);
				EXPECT_EQ(SourceDesc->mName, FirstSourceName);
				EXPECT_EQ(DestinationDesc->mName, FirstDestinationName);
			}
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(Graph.Compile());
			ASSERT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
			const auto Stats = Graph.GetRevisionStats();
			if (!Frame)
			{
				FirstHash = Stats.mConstructionHash;
				FirstRevision = Stats.mPersistentRevision;
			}
			else
			{
				EXPECT_EQ(Stats.mConstructionHash, FirstHash);
				EXPECT_EQ(Stats.mPersistentRevision, FirstRevision);
				EXPECT_EQ(Stats.mCompileCount, 1u);
				EXPECT_EQ(Stats.mCacheHitCount, 1u);
				EXPECT_EQ(Stats.mCachedRevisionCount, 1u);
			}
			// Semantic graphs can compile without a device; the execution attempt releases their edit lock.
			EXPECT_FALSE(Graph.Execute().mStatus);
			EXPECT_FALSE(Graph.GetRevisionStats().mbEditQueueLocked);
		}
	}

	struct FArdaPersistentRevisionProbeParameters
	{
		FArdaDependencyResourceHandle mBuffer;
		uint32_t mValue = 0;
		eastl::shared_ptr<eastl::vector<FArdaRHIBufferRef>> mRecordedStorage;
	};

	class FArdaPersistentRevisionProbeNode final
	    : public TArdaComputeDependencyNode<FArdaPersistentRevisionProbeNode, FArdaPersistentRevisionProbeParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.revision-persistent-storage", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			return FArdaDependencyKeyBuilder()
			    .Resource(Parameters.mBuffer)
			    .Value(Parameters.mValue)
			    .Value(reinterpret_cast<uintptr_t>(Parameters.mRecordedStorage.get()))
			    .Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			Desc.mAccesses = {
			    {Parameters.mBuffer, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}};
			return Desc;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			const auto Buffer = Context.GetBuffer(Parameters.mBuffer);
			if (!Buffer)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "The retained resource handle is invalid.");
			}
			Parameters.mRecordedStorage->push_back(Buffer);
			return Context.GetCommands().ClearBufferUInt(*Buffer, Parameters.mValue);
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaPersistentRevisionProbeNode, ProbeRevisionStorage);
	ARDA_DEFINE_GRAPH_NODE(FArdaPersistentRevisionProbeNode, ProbeRevisionStorage);

	TEST(ArdaGraphRevision, FreshConstructionReusesCacheAndRejectsStaleHandles)
	{
		FArdaDependencyGraph Graph;
		auto RecordedStorage = eastl::make_shared<eastl::vector<FArdaRHIBufferRef>>();
		FArdaDependencyResourceHandle Previous;
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto Buffer = AllocateBuffer(Edit, Desc, "buffer");
			ASSERT_TRUE(Buffer);
			EXPECT_NE(Buffer.GetHandle(), Previous);
			Previous = Buffer.GetHandle();
			ASSERT_TRUE(ProbeRevisionStorage(Edit, {Buffer, 17, RecordedStorage}, "writer"));
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(Graph.Compile());
			EXPECT_NE(Graph.FindResource(Previous), nullptr);
			EXPECT_FALSE(Graph.Execute().mStatus);
		}
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 1u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 2u);
		for (const bool Normalized : {false, true})
		{
			FArdaGraphEdit Edit(Graph);
			auto Stale = Previous;
			if (Normalized)
			{
				Stale = {0, Previous.mIndex, 0};
			}
			else
			{
				Stale.mGeneration += 1000;
			}
			EXPECT_FALSE(ProbeRevisionStorage(Edit, {Stale, 17, RecordedStorage}));
			EXPECT_FALSE(Edit.Commit());
			ASSERT_TRUE(Edit.Cancel());
		}
	}

	TEST(ArdaGraphRevision, SwitchingCachedRevisionsPreservesCurrentConstructionHandles)
	{
		FArdaDependencyGraph Graph;
		eastl::vector<FArdaDependencyResourceHandle> Handles;
		for (const uint8_t Value : {17, 17, 29, 29, 17, 17})
		{
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			const auto Buffer = AllocateBuffer(Edit, Desc, "buffer");
			ASSERT_TRUE(Buffer);
			Handles.push_back(Buffer.GetHandle());
			ASSERT_TRUE(UploadBuffer(Edit, {Buffer, eastl::vector<uint8_t>(16, Value)}, "writer"));
			Edit.Keep(Buffer);
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(Graph.Compile());
			EXPECT_NE(Graph.FindResource(Buffer), nullptr);
			EXPECT_FALSE(Graph.Execute().mStatus);
		}
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 4u);
		EXPECT_NE(Handles[4], Handles[5]);
	}

	/** Expose the active fixture revision only to inject a completion failure after native submission. */

	class FArdaRevisionCompletionFaultRule final : public FArdaGraphPreprocessRule
	{
	public:
		inline static FArdaDependencyGraph::FArdaImpl* mGraph = nullptr;

		const char* GetName() const noexcept override
		{
			return "RevisionCompletionFault";
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override
		{
			mGraph = &FArdaInductorGraphAccess::State(Graph);
			return {};
		}
	};

	class FArdaInferredRevisionGpu : public testing::TestWithParam<const char*>
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
			FArdaRevisionCompletionFaultRule::mGraph = nullptr;
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

		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(FArdaInferredRevisionGpu, ReplayingAConstructionRestoresItsPersistentStorage)
	{
		FArdaDependencyGraph Graph(mDevice);
		auto RecordedStorage = eastl::make_shared<eastl::vector<FArdaRHIBufferRef>>();
		for (const uint32_t Value : {11u, 29u, 11u})
		{
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto Buffer =
			    AllocateBuffer(Edit, Desc, "persistent history", EArdaGraphResourceLifetime::Persistent);
			ASSERT_TRUE(Buffer);
			ASSERT_TRUE(ProbeRevisionStorage(Edit, {Buffer, Value, RecordedStorage}, "writer"));
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(Graph.Compile());
			const auto Execution = Graph.Execute();
			ASSERT_TRUE(Execution.mStatus) << Execution.mStatus.mMessage.c_str();
		}
		ASSERT_EQ(RecordedStorage->size(), 3u);
		EXPECT_NE((*RecordedStorage)[0], (*RecordedStorage)[1]);
		EXPECT_EQ((*RecordedStorage)[2], (*RecordedStorage)[0]);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 1u);
	}

	TEST_P(FArdaInferredRevisionGpu, RecompileRecoversAnOlderFailedSlotAfterTheLatestFrameSucceeded)
	{
		FArdaDependencyGraph Graph(mDevice);
		{
			FArdaGraphEdit Edit(Graph);
			FArdaInductorOptions Options;
			Options.mFramesInFlight = 2;
			Options.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaRevisionCompletionFaultRule>());
			Edit.SetOptions(Options);
			ASSERT_TRUE(Edit.GetStatus());
			const auto UploadOperation = UploadBuffer(Edit, {{}, eastl::vector<uint8_t>(16, uint8_t{53})});
			ASSERT_TRUE(UploadOperation);
			Edit.Keep(UploadOperation.GetBuffer());
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.Compile());
		const auto First = Graph.Submit();
		ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
		const auto Second = Graph.Submit();
		ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
		auto* Revision = FArdaRevisionCompletionFaultRule::mGraph;
		ASSERT_NE(Revision, nullptr);
		ASSERT_TRUE(Revision->mRuntime);
		ASSERT_EQ(Revision->mRuntime->mFrames.size(), 2u);
		auto& OlderCompletion = Revision->mRuntime->mFrames[0]->mActive->mReadbackCompletions;
		auto FailedReadback = OlderCompletion.Register(eastl::make_shared<eastl::vector<uint8_t>>(), 0);
		FailedReadback({{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Injected late readback failure.")});
		EXPECT_FALSE(Graph.Wait(First.mValue).mStatus);
		ASSERT_TRUE(Graph.Wait(Second.mValue).mStatus);
		EXPECT_FALSE(Graph.Submit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 0u);
		const auto Recovered = Graph.Execute();
		EXPECT_TRUE(Recovered.mStatus) << Recovered.mStatus.mMessage.c_str();
	}

	TEST_P(FArdaInferredRevisionGpu, FreshReplayKeepsAliasesEmbeddedInCachedNodeParametersValid)
	{
		FArdaDependencyGraph Graph(mDevice);
		auto RecordedStorage = eastl::make_shared<eastl::vector<FArdaRHIBufferRef>>();
		FArdaDependencyResourceHandle Current;
		for (uint32_t Frame = 0; Frame < 4; ++Frame)
		{
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto Buffer = AllocateBuffer(Edit, Desc, "buffer");
			ASSERT_TRUE(Buffer);
			Current = Buffer.GetHandle();
			ASSERT_TRUE(ProbeRevisionStorage(Edit, {Current, Frame < 2 ? 17u : 29u, RecordedStorage}, "writer"));
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(Graph.Compile());
			const auto Execution = Graph.Execute();
			ASSERT_TRUE(Execution.mStatus) << Execution.mStatus.mMessage.c_str();
		}
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 2u);
		ASSERT_EQ(RecordedStorage->size(), 4u);
		EXPECT_EQ((*RecordedStorage)[0], (*RecordedStorage)[1]);
		EXPECT_EQ((*RecordedStorage)[2], (*RecordedStorage)[3]);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaInferredRevisionGpu, testing::Values("native-d3d12", "native-vulkan"));
}
