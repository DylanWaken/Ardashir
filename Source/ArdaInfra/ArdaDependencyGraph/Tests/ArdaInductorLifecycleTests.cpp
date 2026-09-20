#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaDependencyNode.h"
#include "Inductor/Compiler/ArdaInductorCommandProgram.h"
#include "ArdaDependencyGraph.h"
#include "Graph/DefaultNodes/ArdaDependencyGraphNodes.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "../../ArdaBackend/Private/RHI/Device/ArdaRHIDevicePrivate.h"

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

namespace arda::inductor_lifecycle_tests
{
	using namespace arda;

	// Supply executable identity and estimated work through the public operation contract.
	struct FArdaLifecycleNodeParameters
	{
		uint32_t mCost = 1;
	};

	template <EArdaDependencyNodeKind Kind>
	struct TArdaAdaptiveLifecycleFixtureNode
	    : TArdaDependencyNode<TArdaAdaptiveLifecycleFixtureNode<Kind>, FArdaLifecycleNodeParameters, Kind>
	{
		using FArdaParameters = FArdaLifecycleNodeParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {Kind == EArdaDependencyNodeKind::Graphics ? "test.AdaptiveLifecycle.graphics"
			                                                  : "test.AdaptiveLifecycle.compute",
			    1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			return FArdaDependencyKeyBuilder{}.Value(Parameters.mCost).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mEstimatedCost = Parameters.mCost;
			Desc.mbSideEffect = true;
			return Desc;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	using FArdaAdaptiveGraphicsNode = TArdaAdaptiveLifecycleFixtureNode<EArdaDependencyNodeKind::Graphics>;
	ARDA_DECLARE_GRAPH_NODE(FArdaAdaptiveGraphicsNode, AdaptiveGraphics);
	ARDA_DEFINE_GRAPH_NODE(FArdaAdaptiveGraphicsNode, AdaptiveGraphics);
	using FArdaAdaptiveComputeNode = TArdaAdaptiveLifecycleFixtureNode<EArdaDependencyNodeKind::Compute>;
	ARDA_DECLARE_GRAPH_NODE(FArdaAdaptiveComputeNode, AdaptiveCompute);
	ARDA_DEFINE_GRAPH_NODE(FArdaAdaptiveComputeNode, AdaptiveCompute);

	class FArdaLifecycleBuffer final : public IArdaProviderObject
	{
	public:
		const void* GetIdentity() const noexcept override
		{
			return this;
		}
	};

	class FArdaLifecycleTimer final : public IArdaProviderObject
	{
	public:
		float mSeconds = 0;

		const void* GetIdentity() const noexcept override
		{
			return this;
		}
	};

	/** Real facade, CPU-only storage, and controllable allocation failure. Recording is unsupported. */
	class FArdaLifecycleProvider final : public IArdaRHIProviderDevice
	{
	public:
		bool mbFailAllocation = false;
		bool mbThrowAllocation = false;
		bool mbPollReady = true;
		uint32_t mAllocationAttempts = 0;
		uint32_t mTimerQueryAttempts = 0;
		uint32_t mTimerReadCount = 0;
		float mNextTimerSeconds = 0;
		uint32_t mSubmissionPollCount = 0;
		uint32_t mCommandListAttempts = 0;
		uint32_t mIdleWaitCount = 0;
		eastl::vector<uint64_t> mWaitedSubmissions;
		std::promise<void>* mWaitStarted = nullptr;
		std::shared_future<void> mWaitGate;
		FArdaRHICapabilities mCapabilities;

		static FArdaRHIStatus Unsupported()
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Not used by the CPU lifecycle fixture.");
		}

		const FArdaRHICapabilities& GetCapabilities() const noexcept override
		{
			return mCapabilities;
		}

		EArdaRHINativeResourceType GetTextureImportType() const noexcept override
		{
			return EArdaRHINativeResourceType{};
		}

		EArdaRHINativeResourceType GetBufferImportType() const noexcept override
		{
			return EArdaRHINativeResourceType{};
		}

		FArdaProviderObjectResult CreateBuffer(const FArdaRHIBufferDesc&) override
		{
			++mAllocationAttempts;
			if (mbThrowAllocation)
			{
				throw 1;
			}
			if (mbFailAllocation)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Injected buffer allocation failure.")};
			}
			return {eastl::make_shared<FArdaLifecycleBuffer>(), {}};
		}

		FArdaProviderObjectResult CreateTimerQuery() override
		{
			++mTimerQueryAttempts;
			auto Timer = eastl::make_shared<FArdaLifecycleTimer>();
			Timer->mSeconds = mNextTimerSeconds;
			return {eastl::move(Timer), {}};
		}

		TArdaRHIResult<bool> PollTimerQuery(const FArdaProviderObjectRef&) override
		{
			return {true, {}};
		}

		TArdaRHIResult<float> GetTimerQuerySeconds(const FArdaProviderObjectRef& Object) override
		{
			const auto* Timer = dynamic_cast<FArdaLifecycleTimer*>(Object.get());
			if (!Timer)
			{
				return {{}, Unsupported()};
			}
			++mTimerReadCount;
			return {Timer->mSeconds, {}};
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> QueryBufferMemoryRequirements(const FArdaRHIBufferDesc&) override
		{
			return {{256, 256, 1}, {}};
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc& Desc) override
		{
			return QueryBufferMemoryRequirements(Desc);
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&) override
		{
			return {{}, Unsupported()};
		}

		FArdaRHIStatus BindTextureMemory(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaProviderObjectRef&,
		    uint64_t) override
		{
			return Unsupported();
		}

		FArdaRHIStatus BindBufferMemory(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&,
		    const FArdaProviderObjectRef&,
		    uint64_t) override
		{
			return Unsupported();
		}

#define ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(Method, Parameter)                                                           \
	FArdaProviderObjectResult Method(const Parameter&) override                                                        \
	{                                                                                                                  \
		return {{}, Unsupported()};                                                                                    \
	}
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(CreateTexture, FArdaRHITextureDesc)
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(CreateHeap, FArdaRHIHeapDesc)
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(CreateStagingTexture, FArdaRHIStagingTextureDesc)
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(ImportTexture, FArdaRHINativeTextureImportDesc)
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(ImportBuffer, FArdaRHINativeBufferImportDesc)
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(CreateSampler, FArdaRHISamplerDesc)
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(CreateShader, FArdaRHIShaderDesc)
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(CreateBindingLayout, FArdaRHIBindingLayoutDesc)
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(CreateFramebuffer, FArdaProviderFramebufferCreateInfo)
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(CreateGraphicsPipeline, FArdaProviderGraphicsPipelineCreateInfo)
		ARDA_LIFECYCLE_UNSUPPORTED_OBJECT(CreateComputePipeline, FArdaProviderComputePipelineCreateInfo)
#undef ARDA_LIFECYCLE_UNSUPPORTED_OBJECT

		FArdaProviderObjectResult CreateBindingSet(const FArdaRHIBindingSetDesc&,
		    const FArdaProviderObjectRef&,
		    const eastl::vector<FArdaProviderBinding>&) override
		{
			return {{}, Unsupported()};
		}

		TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaProviderObjectRef&,
		    const FArdaRHITextureSlice&,
		    EArdaRHICpuAccess) override
		{
			return {{}, Unsupported()};
		}

		FArdaRHIStatus UnmapStagingTexture(const FArdaProviderObjectRef&) override
		{
			return Unsupported();
		}

		TArdaRHIResult<void*> MapBuffer(const FArdaProviderObjectRef&, uint64_t, size_t) override
		{
			return {{}, Unsupported()};
		}

		void UnmapBuffer(const FArdaProviderObjectRef&) noexcept override
		{
		}

		TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>> CreateCommandList(EArdaRHIQueueType, bool) override
		{
			++mCommandListAttempts;
			return {{}, Unsupported()};
		}

		TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList&, EArdaRHIQueueType) override
		{
			return {{}, Unsupported()};
		}

		FArdaRHIStatus WaitForIdle() override
		{
			++mIdleWaitCount;
			return {};
		}

		FArdaRHIStatus WaitForSubmission(uint64_t Submission) override
		{
			mWaitedSubmissions.push_back(Submission);
			if (mWaitStarted)
			{
				mWaitStarted->set_value();
			}
			if (mWaitGate.valid())
			{
				mWaitGate.wait();
			}
			return {};
		}

		TArdaRHIResult<bool> PollSubmission(uint64_t) override
		{
			++mSubmissionPollCount;
			return {mbPollReady, {}};
		}

		void RunGarbageCollection() override
		{
		}

		void FlushPipelineCache() noexcept override
		{
		}
	};

	/** Prepared commands and timer refs, with no native command recording or submission. */
	struct FArdaAdaptiveLifecycleFixture
	{
		eastl::shared_ptr<FArdaLifecycleProvider> mProvider = eastl::make_shared<FArdaLifecycleProvider>();
		FArdaDependencyGraph::FArdaImpl mGraph;
		FArdaGraphNodeHandle mIndependent, mProducer, mCompute;

		~FArdaAdaptiveLifecycleFixture()
		{
			CancelArdaInductorTimingOptimization(mGraph);
		}

		FArdaRHIStatus Prepare()
		{
			mProvider->mCapabilities.mQueues.mbCompute = true;
			mProvider->mCapabilities.mQueues.mbGpuWaits = true;
			mProvider->mCapabilities.mQueues.mGraphicsTimestampValidBits = 64;
			mProvider->mCapabilities.mQueues.mComputeTimestampValidBits = 64;
			mGraph.mDevice = CreateArdaRHIDevice(mProvider);
			mGraph.mIdentity = 333;
			mGraph.mCompile.mRevision = 7;
			mGraph.mOptions.mbEnableGpuTiming = true;
			mGraph.mOptions.mbEnableAdaptiveScheduling = true;
			mGraph.mOptions.mMinimumAsyncChain = mGraph.mOptions.mMinimumAsyncSlack = 1;
			mGraph.mOptions.mAdaptiveSchedulingInterval = mGraph.mOptions.mAdaptiveSchedulingMinSamples = 1;
			mGraph.mOptions.mAdaptiveSchedulingSearchBudget = 1;
			mGraph.mOptions.mMaxVramBytes = 1;
			// Assemble through operations, then seed the deliberately suboptimal schedule under test.
			FArdaDependencyGraph Construction(mGraph.mDevice);
			FArdaGraphEdit Edit(Construction);
			const auto Independent = AdaptiveGraphics(Edit, FArdaLifecycleNodeParameters{10}, "independent graphics");
			const auto Producer = AdaptiveGraphics(Edit, FArdaLifecycleNodeParameters{1}, "graphics producer");
			const auto Compute = AdaptiveCompute(Edit, FArdaLifecycleNodeParameters{10}, "compute consumer");
			if (auto Status = Edit.Order(Producer, Compute); !Status)
			{
				return Status;
			}
			if (auto Status = Edit.Commit(); !Status)
			{
				return Status;
			}
			if (auto Status = Construction.Compile(); !Status)
			{
				return Status;
			}
			mGraph.mTopology = Construction.GetTopology().CloneSnapshot();
			mIndependent = Independent.GetHandle();
			mProducer = Producer.GetHandle();
			mCompute = Compute.GetHandle();
			for (const auto Handle : mGraph.mTopology.GetNodes())
			{
				mGraph.mNames[mGraph.mTopology.TryGetNode(Handle)->mPayload.mName] = Handle;
			}
			mGraph.mCompile.mExecutionOrder = {mIndependent, mProducer, mCompute};
			mGraph.mCompile.mQueues = {EArdaRHIQueueType::Graphics,
			    EArdaRHIQueueType::Graphics,
			    EArdaRHIQueueType::Compute};
			FArdaMemoryPlan Plan;
			Plan.mbBudgetAccepted = true;
			const auto Input = FArdaInductorGraphAccess::View(mGraph);
			if (auto Status = FArdaFlattenDispatchesRule{}.Apply(Input, mGraph.mCompile); !Status)
			{
				return Status;
			}
			if (auto Status = FArdaResolveFencesRule{}.Apply(Input, mGraph.mCompile); !Status)
			{
				return Status;
			}
			return FArdaInductorRuntime::Prepare(mGraph, Plan);
		}

		void AddSamples()
		{
			for (const auto H : mGraph.mCompile.mExecutionOrder)
			{
				const auto& N = mGraph.mTopology.TryGetNode(H)->mPayload;
				mGraph.mTiming->Add(N.mName, N.mDesc.mEstimatedCost * .001, {H}, 1);
			}
		}

		template <class Predicate>
		bool AdvanceUntil(Predicate Complete)
		{
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			do
			{
				AdvanceArdaInductorTelemetry(mGraph);
				if (Complete())
				{
					return true;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			} while (std::chrono::steady_clock::now() < Deadline);
			return false;
		}
	};

	/** Capture only the retained graph state, never compiler scratch, to seed deterministic timing input. */
	struct FArdaRevisionLifecycleCaptureRule : FArdaGraphPreprocessRule
	{
		static inline FArdaDependencyGraph::FArdaImpl* mGraph = nullptr;

		const char* GetName() const noexcept override
		{
			return "CaptureRevisionLifecycle";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override
		{
			mGraph = &FArdaInductorGraphAccess::State(Graph);
			return {};
		}
	};

	template <EArdaDependencyNodeKind Kind>
	struct TArdaRevisionLifecycleNode
	    : TArdaDependencyNode<TArdaRevisionLifecycleNode<Kind>, FArdaLifecycleNodeParameters, Kind>
	{
		using FArdaParameters = FArdaLifecycleNodeParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {Kind == EArdaDependencyNodeKind::Graphics ? "test.RevisionLifecycle.graphics"
			                                                  : "test.RevisionLifecycle.compute",
			    1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			return FArdaDependencyKeyBuilder{}.Value(Parameters.mCost).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mEstimatedCost = Parameters.mCost;
			Desc.mbSideEffect = true;
			return Desc;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	using FArdaRevisionGraphicsNode = TArdaRevisionLifecycleNode<EArdaDependencyNodeKind::Graphics>;
	ARDA_DECLARE_GRAPH_NODE(FArdaRevisionGraphicsNode, RevisionGraphics);
	ARDA_DEFINE_GRAPH_NODE(FArdaRevisionGraphicsNode, RevisionGraphics);
	using FArdaRevisionComputeNode = TArdaRevisionLifecycleNode<EArdaDependencyNodeKind::Compute>;
	ARDA_DECLARE_GRAPH_NODE(FArdaRevisionComputeNode, RevisionCompute);
	ARDA_DEFINE_GRAPH_NODE(FArdaRevisionComputeNode, RevisionCompute);

	TEST(ArdaInductorLifecycle, FrameReconstructionPreservesBackgroundTuningAndPersistentIdentity)
	{
		auto Provider = eastl::make_shared<FArdaLifecycleProvider>();
		Provider->mCapabilities.mQueues.mbCompute = true;
		Provider->mCapabilities.mQueues.mbGpuWaits = true;
		Provider->mCapabilities.mQueues.mGraphicsTimestampValidBits = 64;
		Provider->mCapabilities.mQueues.mComputeTimestampValidBits = 64;
		FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
		FArdaInductorOptions Options;
		Options.mbEnableGpuTiming = true;
		Options.mMinimumAsyncChain = Options.mMinimumAsyncSlack = 1;
		Options.mAdaptiveSchedulingSearchBudget = 1;
		Options.mMaxVramBytes = 1;
		Options.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaRevisionLifecycleCaptureRule>());
		const auto RecordFrame = [&]()
		{
			FArdaGraphEdit Edit(Graph);
			Edit.ReportError(Edit.SetOptions(Options));
			RevisionGraphics(Edit, uint32_t(10));
			const auto Producer = RevisionGraphics(Edit, uint32_t(1));
			const auto Consumer = RevisionCompute(Edit, uint32_t(10));
			Edit.ReportError(Edit.Order(Producer, Consumer));
			return Edit.Commit();
		};
		FArdaRevisionLifecycleCaptureRule::mGraph = nullptr;
		ASSERT_TRUE(RecordFrame());
		ASSERT_TRUE(Graph.Compile());
		auto* Persistent = FArdaRevisionLifecycleCaptureRule::mGraph;
		ASSERT_NE(Persistent, nullptr);
		const auto StructureRevision = Graph.GetRevisionStats().mPersistentRevision;
		const auto ConstructionHash = Graph.GetRevisionStats().mConstructionHash;
		const auto ScheduleRevision = Graph.GetCompileResult().mRevision;
		const auto Nodes = Graph.GetTopology().GetNodes();
		ASSERT_EQ(Nodes.size(), 3u);
		// This fixture tests CPU lifecycle without native command recording. Consume the execution
		// boundary, then prepare the same known suboptimal schedule used by the adaptive unit fixture.
		Persistent->mPendingMemoryPlan.reset();
		EXPECT_FALSE(Graph.Execute().mStatus);
		Persistent->mCompile = {};
		Persistent->mCompile.mRevision = ScheduleRevision;
		Persistent->mCompile.mExecutionOrder = Nodes;
		Persistent->mCompile.mQueues = {EArdaRHIQueueType::Graphics,
		    EArdaRHIQueueType::Graphics,
		    EArdaRHIQueueType::Compute};
		FArdaMemoryPlan Plan;
		Plan.mbBudgetAccepted = true;
		const auto Input = FArdaInductorGraphAccess::View(*Persistent);
		ASSERT_TRUE(FArdaFlattenDispatchesRule{}.Apply(Input, Persistent->mCompile));
		ASSERT_TRUE(FArdaResolveFencesRule{}.Apply(Input, Persistent->mCompile));
		ASSERT_TRUE(FArdaInductorRuntime::Prepare(*Persistent, Plan));
		for (const auto Node : Nodes)
		{
			const auto& Definition = Persistent->mTopology.TryGetNode(Node)->mPayload;
			Persistent->mTiming->Add(Definition.mName, Definition.mDesc.mEstimatedCost * .001, {Node}, 1);
		}
		ASSERT_TRUE(Graph.OptimizeFromTimingProfile());
		const auto Pending = Persistent->mAdaptiveJob;
		ASSERT_TRUE(Pending);
		ASSERT_TRUE(RecordFrame());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Persistent->mAdaptiveJob, Pending);
		EXPECT_TRUE(Graph.GetAdaptiveSchedulingStats().mbPending);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 1u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 1u);
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (Graph.GetAdaptiveSchedulingStats().mbPending && std::chrono::steady_clock::now() < Deadline)
		{
			Graph.CollectTelemetry();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		EXPECT_FALSE(Graph.GetAdaptiveSchedulingStats().mbPending);
		EXPECT_EQ(Graph.GetAdaptiveSchedulingStats().mAcceptedSchedules, 1u);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.front(), Nodes[1]);
		EXPECT_EQ(Graph.GetCompileResult().mRevision, ScheduleRevision + 1);
		EXPECT_EQ(Graph.GetRevisionStats().mPersistentRevision, StructureRevision);
		EXPECT_EQ(Graph.GetRevisionStats().mConstructionHash, ConstructionHash);
		FArdaRevisionLifecycleCaptureRule::mGraph = nullptr;
	}

	TEST(ArdaInductorLifecycle, BackgroundTuningDefersAdoptionUntilReadyAndReusesThePreparedPool)
	{
		FArdaAdaptiveLifecycleFixture F;
		ASSERT_TRUE(F.Prepare());
		ASSERT_EQ(F.mProvider->mTimerQueryAttempts, 3u);
		const auto OriginalOrder = F.mGraph.mCompile.mExecutionOrder;
		const auto* OriginalRuntime = F.mGraph.mRuntime.get();
		eastl::unordered_map<uint32_t, FArdaRHITimerQueryRef> OriginalTimers;
		for (const auto& Timer : F.mGraph.mRuntime->mFrames.front()->mTimers)
		{
			OriginalTimers.emplace(Timer.mNodes.front().GetIndex(), Timer.mQuery);
		}
		auto Pending = eastl::make_shared<FArdaDependencyFrameTicket::FArdaState>();
		Pending->mDevice = F.mGraph.mDevice;
		Pending->mSequence = 1;
		Pending->mExecution.mLastSubmittedInstances = {41, 0, 0};
		F.mGraph.mRuntime->mFrames.front()->mActive = Pending;
		F.mProvider->mbPollReady = false;
		F.AddSamples();
		F.mGraph.mNextFrameSequence = 2;
		// This poll is reached only after the worker publishes its prepared proposal;
		// the fake frame has no telemetry that could poll its submission earlier.
		ASSERT_TRUE(F.AdvanceUntil(
		    [&]
		    {
			    return F.mProvider->mSubmissionPollCount != 0;
		    }));
		EXPECT_TRUE(F.mGraph.mAdaptiveStats.mbPending);
		EXPECT_EQ(F.mGraph.mCompile.mExecutionOrder, OriginalOrder);
		EXPECT_EQ(F.mGraph.mRuntime.get(), OriginalRuntime);
		EXPECT_EQ(F.mGraph.mAdaptiveStats.mAcceptedSchedules, 0u);
		EXPECT_TRUE(F.mProvider->mWaitedSubmissions.empty());
		F.mProvider->mbPollReady = true;
		ASSERT_TRUE(F.AdvanceUntil(
		    [&]
		    {
			    return F.mGraph.mAdaptiveStats.mAcceptedSchedules == 1;
		    }));
		EXPECT_TRUE(Pending->mbComplete);
		EXPECT_FALSE(F.mGraph.mAdaptiveStats.mbPending);
		EXPECT_EQ(F.mGraph.mAdaptiveStats.mCandidatesExamined, 1u);
		EXPECT_NE(F.mGraph.mCompile.mExecutionOrder, OriginalOrder);
		EXPECT_EQ(F.mGraph.mCompile.mExecutionOrder.front(), F.mProducer);
		EXPECT_EQ(F.mGraph.mCompile.mRevision, 8u);
		EXPECT_NEAR(F.mGraph.mCompile.mEstimatedExecutionCost, 11, 1e-10);
		EXPECT_EQ(F.mGraph.mRuntime->mBinding->mGraph, &F.mGraph);
		for (const auto& Timer : F.mGraph.mRuntime->mFrames.front()->mTimers)
		{
			EXPECT_EQ(Timer.mQuery, OriginalTimers.at(Timer.mNodes.front().GetIndex()));
		}
		EXPECT_EQ(F.mProvider->mTimerQueryAttempts, 3u);
		EXPECT_EQ(F.mProvider->mAllocationAttempts, 0u);
		EXPECT_EQ(F.mProvider->mCommandListAttempts, 0u);
		EXPECT_EQ(F.mProvider->mIdleWaitCount, 0u);
		EXPECT_TRUE(F.mProvider->mWaitedSubmissions.empty());
	}

	TEST(ArdaInductorLifecycle, ClearingTelemetryAndCancelingWorkCannotAdoptAnObsoleteProposal)
	{
		FArdaAdaptiveLifecycleFixture F;
		ASSERT_TRUE(F.Prepare());
		const auto OriginalOrder = F.mGraph.mCompile.mExecutionOrder;
		const auto* OriginalRuntime = F.mGraph.mRuntime.get();
		F.AddSamples();
		F.mGraph.mNextFrameSequence = 2;
		AdvanceArdaInductorTelemetry(F.mGraph);
		ASSERT_TRUE(F.mGraph.mAdaptiveStats.mbPending);
		F.mGraph.mTiming->Clear();
		ASSERT_TRUE(F.AdvanceUntil(
		    [&]
		    {
			    return !F.mGraph.mAdaptiveStats.mbPending;
		    }));
		EXPECT_EQ(F.mGraph.mCompile.mExecutionOrder, OriginalOrder);
		EXPECT_EQ(F.mGraph.mCompile.mRevision, 7u);
		EXPECT_EQ(F.mGraph.mRuntime.get(), OriginalRuntime);
		EXPECT_EQ(F.mGraph.mAdaptiveStats.mAcceptedSchedules, 0u);
		F.AddSamples();
		const auto Seed = F.mGraph.mRuntime->mAdaptiveSeed;
		const auto Reuse = F.mGraph.mRuntime->mReuseFrames;
		const auto SeedOwners = Seed.use_count(), ReuseOwners = Reuse.use_count();
		ASSERT_TRUE(RequestArdaInductorTimingOptimization(F.mGraph, false));
		CancelArdaInductorTimingOptimization(F.mGraph);
		EXPECT_FALSE(F.mGraph.mAdaptiveJob);
		EXPECT_FALSE(F.mGraph.mAdaptiveStats.mbPending);
		EXPECT_EQ(Seed.use_count(), SeedOwners);
		EXPECT_EQ(Reuse.use_count(), ReuseOwners);
		EXPECT_EQ(F.mGraph.mCompile.mRevision, 7u);
		EXPECT_EQ(F.mGraph.mRuntime.get(), OriginalRuntime);
		EXPECT_EQ(F.mProvider->mTimerQueryAttempts, 3u);
		EXPECT_EQ(F.mProvider->mAllocationAttempts, 0u);
		EXPECT_EQ(F.mProvider->mCommandListAttempts, 0u);
		EXPECT_EQ(F.mProvider->mIdleWaitCount, 0u);
		EXPECT_TRUE(F.mProvider->mWaitedSubmissions.empty());
	}

	TEST(ArdaInductorLifecycle, DiscardingFailedConstructionPreservesTheUnmaterializedPlan)
	{
		auto Provider = eastl::make_shared<FArdaLifecycleProvider>();
		FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		FArdaGraphEdit Edit(Graph);
		const auto Original = AllocateBuffer(Edit, Desc, "original");
		ASSERT_TRUE(Original);
		const auto Writer = ClearNode(Edit, FArdaGraphClearParameters{Original.GetHandle(), 7}, "original writer");
		ASSERT_TRUE(Writer);
		Edit.Keep(Original.GetHandle());
		ASSERT_TRUE(Edit.Commit());
		const auto Initial = Graph.Compile();
		ASSERT_TRUE(Initial) << Initial.mMessage.c_str();
		ASSERT_EQ(Provider->mAllocationAttempts, 0u);
		const auto Revision = Graph.GetCompileResult().mRevision;
		const auto OriginalOrder = Graph.GetCompileResult().mExecutionOrder;

		Provider->mbFailAllocation = true;
		// Consume the staging lock with a failed allocation; the plan remains unmaterialized.
		EXPECT_EQ(Graph.Submit().mStatus.mCode, EArdaRHIResult::BackendFailure);
		FArdaGraphEdit InvalidEdit(Graph);
		const auto Abandoned = AllocateBuffer(InvalidEdit, Desc, "abandoned");
		ASSERT_TRUE(Abandoned);
		const auto OtherBuffer = AllocateBuffer(InvalidEdit, Desc, "other");
		InvalidEdit.Keep(Abandoned);
		InvalidEdit.Keep(OtherBuffer);
		const auto First = ClearNode(InvalidEdit, FArdaGraphClearParameters{Abandoned, 7}, "first writer");
		const auto Other = ClearNode(InvalidEdit, FArdaGraphClearParameters{OtherBuffer, 0}, "other writer");
		ASSERT_TRUE(First);
		ASSERT_TRUE(Other);
		ASSERT_TRUE(InvalidEdit.Order(First, Other));
		ASSERT_TRUE(InvalidEdit.Order(Other, First));
		ASSERT_TRUE(InvalidEdit.Commit());
		const auto CompileFailure = Graph.Compile();
		ASSERT_FALSE(CompileFailure);
		const auto Restored = Graph.DiscardPendingEdits();
		ASSERT_TRUE(Restored) << Restored.mMessage.c_str();
		EXPECT_FALSE(Graph.IsEditing());
		EXPECT_EQ(Graph.FindResource(Abandoned.GetHandle()), nullptr);
		EXPECT_NE(Graph.FindResource(Original.GetHandle()), nullptr);
		EXPECT_EQ(Graph.FindNode("original writer"), Writer.GetHandle());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, OriginalOrder);
		EXPECT_EQ(Provider->mAllocationAttempts, 1u);
		EXPECT_EQ(Provider->mCommandListAttempts, 0u);
		EXPECT_FALSE(InvalidEdit.Cancel());
		FArdaGraphEdit CanceledEdit(Graph);
		ASSERT_TRUE(CanceledEdit.Cancel());
		EXPECT_EQ(Provider->mAllocationAttempts, 1u);
		// Cancellation retained the committed plan, ready to materialize on submission.
		EXPECT_EQ(Graph.Submit().mStatus.mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(Provider->mAllocationAttempts, 2u);
	}

	TEST(ArdaInductorLifecycle, FailedOrThrowingMaterializationRetainsThePlanForRetry)
	{
		for (const bool Throw : {false, true})
		{
			auto Provider = eastl::make_shared<FArdaLifecycleProvider>();
			FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 256;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto Buffer = AllocateBuffer(Edit, Desc, "retry");
			ASSERT_TRUE(Buffer);
			ASSERT_TRUE(ClearBuffer(Edit, {Buffer.GetHandle(), 7}, "clear"));
			Edit.Keep(Buffer.GetHandle());
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(Graph.Compile());
			const auto Revision = Graph.GetCompileResult().mRevision;
			EXPECT_EQ(Provider->mAllocationAttempts, 0u);
			Provider->mbFailAllocation = !Throw;
			Provider->mbThrowAllocation = Throw;
			for (uint32_t Attempt = 1; Attempt <= 2; ++Attempt)
			{
				EXPECT_EQ(Graph.Execute().mStatus.mCode, EArdaRHIResult::BackendFailure);
				EXPECT_EQ(Provider->mAllocationAttempts, Attempt);
				EXPECT_EQ(Provider->mCommandListAttempts, 0u);
				EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
			}
			Provider->mbFailAllocation = Provider->mbThrowAllocation = false;
			// This provider does not record commands. Reaching recording proves the retry materialized.
			EXPECT_EQ(Graph.Execute().mStatus.mCode, EArdaRHIResult::Unsupported);
			EXPECT_EQ(Provider->mAllocationAttempts, 3u);
			EXPECT_GT(Provider->mCommandListAttempts, 0u);
		}
	}

	TEST(ArdaInductorLifecycle, FrameRetirementWaitsOnlyItsSubmissionsAndKeepsTheCompletedReceipt)
	{
		auto Provider = eastl::make_shared<FArdaLifecycleProvider>();
		FArdaDependencyFrameTicket::FArdaState State;
		State.mDevice = CreateArdaRHIDevice(Provider);
		State.mExecution.mLastSubmittedInstances = {13, 27, 0};
		auto Output = eastl::make_shared<eastl::vector<uint8_t>>();
		auto Callback = State.mReadbackCompletions.Register(Output, 1);
		Callback({{4, 2}, {}});
		ASSERT_TRUE(FArdaInductorRuntime::WaitState(State).mStatus);
		EXPECT_EQ(Provider->mWaitedSubmissions, (eastl::vector<uint64_t>{13, 27}));
		EXPECT_EQ(*Output, (eastl::vector<uint8_t>{4, 2}));
		ASSERT_TRUE(FArdaInductorRuntime::WaitState(State).mStatus);
		EXPECT_EQ(Provider->mWaitedSubmissions.size(), 2u);
		EXPECT_EQ(Provider->mIdleWaitCount, 0u);
	}

	TEST(ArdaInductorLifecycle, WaitingCompletedFramesOutOfOrderKeepsEveryTimingSampleExactlyOnce)
	{
		auto Provider = eastl::make_shared<FArdaLifecycleProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		auto Profile = eastl::make_shared<FArdaInductorTimingAccumulator>();
		Profile->Configure(.5, 2);
		const FArdaGraphNodeHandle Node(1, 1, 123);
		const eastl::array<EArdaRHIQueueType, 4> Queues = {EArdaRHIQueueType::Compute,
		    EArdaRHIQueueType::Copy,
		    EArdaRHIQueueType::Graphics,
		    EArdaRHIQueueType::Compute};
		eastl::vector<eastl::unique_ptr<FArdaDependencyFrameTicket::FArdaState>> Frames;
		for (uint32_t I = 0; I < Queues.size(); ++I)
		{
			Provider->mNextTimerSeconds = float(1 + 2 * I);
			const auto Timer = Device->CreateTimerQuery();
			ASSERT_TRUE(Timer);
			auto Frame = eastl::make_unique<FArdaDependencyFrameTicket::FArdaState>();
			Frame->mDevice = Device;
			Frame->mSequence = I + 1;
			Frame->mTiming = Profile;
			Frame->mTimingEpoch = Profile->GetEpoch();
			Frame->mbSampleTiming = true;
			Frame->mExecution.mLastSubmittedInstances[GetArdaRHIQueueIndex(Queues[I])] = 11 + I;
			Frame->mTimers.push_back({"node", Timer.mValue, {Node}, Queues[I]});
			Frames.push_back(eastl::move(Frame));
		}
		// Waiting a newer ticket first is legal. Both native records must be consumed.
		ASSERT_TRUE(FArdaInductorRuntime::WaitState(*Frames[1]).mStatus);
		ASSERT_TRUE(FArdaInductorRuntime::WaitState(*Frames[0]).mStatus);
		auto Samples = Profile->Snapshot();
		ASSERT_EQ(Samples.size(), 1u);
		EXPECT_EQ(Samples[0].mSampleCount, 2u);
		EXPECT_DOUBLE_EQ(Samples[0].mGpuSeconds, 2);
		EXPECT_DOUBLE_EQ(Samples[0].mMinGpuSeconds, 1);
		EXPECT_DOUBLE_EQ(Samples[0].mMaxGpuSeconds, 3);
		EXPECT_DOUBLE_EQ(Samples[0].mLastGpuSeconds, 3);
		EXPECT_EQ(Samples[0].mLastFrameSequence, 2u);
		EXPECT_EQ(Samples[0].mQueue, EArdaRHIQueueType::Copy);
		auto History = Profile->History();
		ASSERT_EQ(History.size(), 2u);
		EXPECT_EQ(History[0].mFrameSequence, 1u);
		EXPECT_EQ(History[1].mFrameSequence, 2u);
		EXPECT_TRUE(Frames[0]->mTimers.empty());
		EXPECT_TRUE(Frames[1]->mTimers.empty());
		ASSERT_TRUE(FArdaInductorRuntime::WaitState(*Frames[1]).mStatus);
		ASSERT_TRUE(FArdaInductorRuntime::WaitState(*Frames[0]).mStatus);
		EXPECT_EQ(Provider->mTimerReadCount, 2u);
		EXPECT_EQ(Profile->Snapshot()[0].mSampleCount, 2u);
		ASSERT_TRUE(FArdaInductorRuntime::WaitState(*Frames[2]).mStatus);
		Samples = Profile->Snapshot();
		EXPECT_EQ(Samples[0].mSampleCount, 3u);
		EXPECT_DOUBLE_EQ(Samples[0].mGpuSeconds, 3.5);
		EXPECT_DOUBLE_EQ(Samples[0].mLastGpuSeconds, 5);
		EXPECT_EQ(Samples[0].mLastFrameSequence, 3u);
		EXPECT_EQ(Samples[0].mQueue, EArdaRHIQueueType::Graphics);
		History = Profile->History();
		ASSERT_EQ(History.size(), 2u);
		// Capacity retains the last two collected samples, even when one frame is older.
		EXPECT_EQ(History[0].mFrameSequence, 1u);
		EXPECT_EQ(History[1].mFrameSequence, 3u);
		Profile->Clear();
		ASSERT_TRUE(FArdaInductorRuntime::WaitState(*Frames[3]).mStatus);
		EXPECT_EQ(Provider->mTimerReadCount, 4u);
		EXPECT_TRUE(Profile->Snapshot().empty());
		EXPECT_TRUE(Profile->History().empty());
		EXPECT_EQ(Provider->mWaitedSubmissions, (eastl::vector<uint64_t>{12, 11, 13, 14}));
		EXPECT_EQ(Provider->mIdleWaitCount, 0u);
	}

	TEST(ArdaInductorLifecycle, RetiringAnIndependentFrameDoesNotPublishAnotherFramesReadback)
	{
		auto Provider = eastl::make_shared<FArdaLifecycleProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		FArdaDependencyFrameTicket::FArdaState First, Second;
		First.mDevice = Second.mDevice = Device;
		First.mExecution.mLastSubmittedInstances = {11, 0, 0};
		Second.mExecution.mLastSubmittedInstances = {22, 0, 0};
		auto FirstOutput = eastl::make_shared<eastl::vector<uint8_t>>();
		auto SecondOutput = eastl::make_shared<eastl::vector<uint8_t>>();
		auto FirstCallback = First.mReadbackCompletions.Register(FirstOutput, 1);
		auto SecondCallback = Second.mReadbackCompletions.Register(SecondOutput, 1);
		FirstCallback({{1}, {}});
		ASSERT_TRUE(FArdaInductorRuntime::WaitState(First).mStatus);
		EXPECT_EQ(*FirstOutput, (eastl::vector<uint8_t>{1}));
		EXPECT_TRUE(SecondOutput->empty());
		EXPECT_FALSE(Second.mReadbackCompletions.IsReady());
		EXPECT_EQ(Provider->mWaitedSubmissions, (eastl::vector<uint64_t>{11}));
		SecondCallback({{2}, {}});
		ASSERT_TRUE(FArdaInductorRuntime::WaitState(Second).mStatus);
		EXPECT_EQ(*SecondOutput, (eastl::vector<uint8_t>{2}));
		EXPECT_EQ(Provider->mIdleWaitCount, 0u);
	}

	TEST(ArdaInductorLifecycle, FailedFrameCanBeRetiredForRepairWithoutDeviceIdle)
	{
		auto Provider = eastl::make_shared<FArdaLifecycleProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		{
			FArdaInductorRuntime Runtime;
			auto Frame = eastl::make_unique<FArdaInductorFrame>();
			Frame->mActive = eastl::make_shared<FArdaDependencyFrameTicket::FArdaState>();
			Frame->mActive->mDevice = Device;
			Frame->mActive->mExecution.mLastSubmittedInstances = {31, 0, 0};
			Frame->mActive->mExecution.mStatus = FArdaLifecycleProvider::Unsupported();
			Runtime.mFrames.push_back(eastl::move(Frame));
			ASSERT_TRUE(Runtime.WaitAll());
			EXPECT_FALSE(Runtime.mFrames.front()->mActive->mExecution.mStatus);
		}
		EXPECT_EQ(Provider->mWaitedSubmissions, (eastl::vector<uint64_t>{31}));
		EXPECT_EQ(Provider->mIdleWaitCount, 0u);
	}

	TEST(ArdaInductorLifecycle, AutomaticRetirementPublishesInFrameOrderAfterSlotWraparound)
	{
		auto Provider = eastl::make_shared<FArdaLifecycleProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		auto Output = eastl::make_shared<eastl::vector<uint8_t>>();
		FArdaInductorRuntime Runtime;
		for (const uint64_t Sequence : {4, 2, 3})
		{
			auto Frame = eastl::make_unique<FArdaInductorFrame>();
			Frame->mActive = eastl::make_shared<FArdaDependencyFrameTicket::FArdaState>();
			Frame->mActive->mDevice = Device;
			Frame->mActive->mSequence = Sequence;
			Frame->mActive->mExecution.mLastSubmittedInstances = {Sequence, 0, 0};
			auto Callback = Frame->mActive->mReadbackCompletions.Register(Output, 1);
			Callback({{static_cast<uint8_t>(Sequence)}, {}});
			Runtime.mFrames.push_back(eastl::move(Frame));
		}
		ASSERT_TRUE(Runtime.WaitAll());
		EXPECT_EQ(Provider->mWaitedSubmissions, (eastl::vector<uint64_t>{2, 3, 4}));
		EXPECT_EQ(*Output, (eastl::vector<uint8_t>{4}));
		EXPECT_EQ(Provider->mIdleWaitCount, 0u);
	}

	TEST(ArdaInductorLifecycle, PollingAFrameDoesNotBlockBehindItsPendingGpuWait)
	{
		using namespace std::chrono_literals;
		auto Provider = eastl::make_shared<FArdaLifecycleProvider>();
		FArdaDependencyGraph::FArdaImpl Graph;
		Graph.mIdentity = 91;
		Graph.mDevice = CreateArdaRHIDevice(Provider);
		Graph.mRuntime = eastl::make_unique<FArdaInductorRuntime>();
		auto Frame = eastl::make_unique<FArdaInductorFrame>();
		Frame->mLowered = eastl::make_unique<FArdaInductorCommandProgram>(Graph.mDevice);
		(void)Frame->mLowered->Finalize();
		Graph.mRuntime->mFrames.push_back(eastl::move(Frame));
		const auto Submitted = FArdaInductorRuntime::Submit(Graph, {});
		ASSERT_TRUE(Submitted.mValue);
		// Model one accepted native submission whose completion is controlled by the fixture.
		auto& Active = *Graph.mRuntime->mFrames.front()->mActive;
		Active.mExecution.mStatus = {};
		Active.mExecution.mLastSubmittedInstances = {17, 0, 0};
		std::promise<void> Started, Complete;
		auto StartedFuture = Started.get_future();
		Provider->mWaitStarted = &Started;
		Provider->mWaitGate = Complete.get_future().share();
		auto Waiter = std::async(std::launch::async,
		    [&]
		    {
			    return FArdaInductorRuntime::Wait(Graph, Submitted.mValue);
		    });
		EXPECT_EQ(StartedFuture.wait_for(1s), std::future_status::ready);
		auto Poll = std::async(std::launch::async,
		    [&]
		    {
			    return FArdaInductorRuntime::IsComplete(Graph, Submitted.mValue);
		    });
		const auto PolledBeforeCompletion = Poll.wait_for(100ms);
		Complete.set_value();
		const auto Polled = Poll.get();
		EXPECT_EQ(PolledBeforeCompletion, std::future_status::ready);
		EXPECT_TRUE(Polled.mStatus);
		EXPECT_FALSE(Polled.mValue);
		EXPECT_TRUE(Waiter.get().mStatus);
		Provider->mWaitStarted = nullptr;
		EXPECT_TRUE(FArdaInductorRuntime::IsComplete(Graph, Submitted.mValue).mValue);
		EXPECT_EQ(Provider->mIdleWaitCount, 0u);
	}
}
