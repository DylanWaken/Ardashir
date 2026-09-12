#include "ArdaInductorCommandProgram.h"
#include "ArdaDependencyGraph.h"
#include "ArdaDependencyGraphNodes.h"
#include "ArdaDependencyGraphInternal.h"
#include "../../ArdaBackend/Private/RHI/ArdaRHIDevicePrivate.h"

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

namespace
{
	using namespace arda;

	class FLifecycleBuffer final : public IArdaProviderObject
	{
	public:
		const void* GetIdentity() const noexcept override
		{
			return this;
		}
	};

	class FLifecycleTimer final : public IArdaProviderObject
	{
	public:
		float mSeconds = 0;

		const void* GetIdentity() const noexcept override
		{
			return this;
		}
	};

	/** Real facade, CPU-only storage, and controllable allocation failure. Recording is unsupported. */
	class FLifecycleProvider final : public IArdaRHIProviderDevice
	{
	public:
		bool mbFailAllocation = false;
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
			if (mbFailAllocation)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Injected buffer allocation failure.")};
			}
			return {eastl::make_shared<FLifecycleBuffer>(), {}};
		}

		FArdaProviderObjectResult CreateTimerQuery() override
		{
			++mTimerQueryAttempts;
			auto Timer = eastl::make_shared<FLifecycleTimer>();
			Timer->mSeconds = mNextTimerSeconds;
			return {eastl::move(Timer), {}};
		}

		TArdaRHIResult<bool> PollTimerQuery(const FArdaProviderObjectRef&) override
		{
			return {true, {}};
		}

		TArdaRHIResult<float> GetTimerQuerySeconds(const FArdaProviderObjectRef& Object) override
		{
			const auto* Timer = dynamic_cast<FLifecycleTimer*>(Object.get());
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
	struct FAdaptiveLifecycleFixture
	{
		eastl::shared_ptr<FLifecycleProvider> mProvider = eastl::make_shared<FLifecycleProvider>();
		FArdaDependencyGraph::FImpl mGraph;
		FArdaGraphNodeHandle mIndependent, mProducer, mCompute;

		~FAdaptiveLifecycleFixture()
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
			const auto Add = [&](const char* Name, EArdaDependencyNodeKind Kind, uint32_t Cost)
			{
				auto Definition = eastl::make_shared<FArdaDependencyNodeDefinition>();
				Definition->mName = Name;
				Definition->mKind = Kind;
				FArdaDependencyNode Node;
				Node.mName = Node.mCanonicalKey = Name;
				Node.mDefinition = Definition;
				Node.mDesc.mEstimatedCost = Cost;
				Node.mDesc.mbSideEffect = true;
				const auto Handle = mGraph.mTopology.AddNode(eastl::move(Node));
				mGraph.mNames[Name] = Handle;
				return Handle;
			};
			mIndependent = Add("independent graphics", EArdaDependencyNodeKind::Graphics, 10);
			mProducer = Add("graphics producer", EArdaDependencyNodeKind::Graphics, 1);
			mCompute = Add("compute consumer", EArdaDependencyNodeKind::Compute, 10);
			mGraph.mTopology.AddEdge(mProducer, mCompute, {});
			mGraph.mCompile.mExecutionOrder = {mIndependent, mProducer, mCompute};
			mGraph.mCompile.mQueues = {EArdaRHIQueueType::Graphics,
			    EArdaRHIQueueType::Graphics,
			    EArdaRHIQueueType::Compute};
			FArdaInductorMemoryPlan Plan;
			Plan.mbBudgetAccepted = true;
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

	TEST(ArdaInductorLifecycle, BackgroundTuningDefersAdoptionUntilReadyAndReusesThePreparedPool)
	{
		FAdaptiveLifecycleFixture F;
		ASSERT_TRUE(F.Prepare());
		ASSERT_EQ(F.mProvider->mTimerQueryAttempts, 3u);
		const auto OriginalOrder = F.mGraph.mCompile.mExecutionOrder;
		const auto* OriginalRuntime = F.mGraph.mRuntime.get();
		eastl::unordered_map<uint32_t, FArdaRHITimerQueryRef> OriginalTimers;
		for (const auto& Timer : F.mGraph.mRuntime->mFrames.front()->mTimers)
		{
			OriginalTimers.emplace(Timer.mNodes.front().GetIndex(), Timer.mQuery);
		}
		auto Pending = eastl::make_shared<FArdaDependencyFrameTicket::FState>();
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
		FAdaptiveLifecycleFixture F;
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

	TEST(ArdaInductorLifecycle, FailedNativeRollbackCanBeRetriedWithoutLosingTheSavedGraph)
	{
		auto Provider = eastl::make_shared<FLifecycleProvider>();
		FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Original = Graph.CreateBuffer("original", Desc);
		ASSERT_TRUE(Original);
		const auto Writer =
		    Graph.AttachOrFind("original writer", "arda.clear-buffer", FArdaGraphClearParameters{Original.mValue, 7});
		ASSERT_TRUE(Writer);
		ASSERT_TRUE(Graph.MarkOutput(Original.mValue));
		const auto Initial = Graph.EndGraphEdit();
		ASSERT_TRUE(Initial) << Initial.mMessage.c_str();
		ASSERT_EQ(Provider->mAllocationAttempts, 1u);
		const auto Revision = Graph.GetCompileResult().mRevision;
		const auto OriginalOrder = Graph.GetCompileResult().mExecutionOrder;

		ASSERT_TRUE(Graph.BeginGraphEdit());
		Provider->mbFailAllocation = true;
		// The next compile retires the existing pool before the injected allocation failure.
		const auto CompileFailure = Graph.EndGraphEdit();
		ASSERT_FALSE(CompileFailure);
		EXPECT_EQ(CompileFailure.mCode, EArdaRHIResult::BackendFailure);
		for (uint32_t Attempt = 0; Attempt < 2; ++Attempt)
		{
			const auto Abandoned = Graph.CreateBuffer("abandoned between cancellation attempts", Desc);
			ASSERT_TRUE(Abandoned);
			const auto CancelFailure = Graph.CancelGraphEdit();
			ASSERT_FALSE(CancelFailure);
			EXPECT_EQ(CancelFailure.mCode, EArdaRHIResult::BackendFailure);
			EXPECT_TRUE(Graph.IsEditing());
			EXPECT_EQ(Graph.FindResource(Abandoned.mValue), nullptr);
			EXPECT_NE(Graph.FindResource(Original.mValue), nullptr);
			EXPECT_EQ(Graph.FindNode("original writer"), Writer.mValue);
			EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, OriginalOrder);
		}
		Provider->mbFailAllocation = false;
		const auto Restored = Graph.CancelGraphEdit();
		ASSERT_TRUE(Restored) << Restored.mMessage.c_str();
		EXPECT_FALSE(Graph.IsEditing());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, OriginalOrder);
		EXPECT_EQ(Provider->mAllocationAttempts, 5u);
		EXPECT_EQ(Provider->mCommandListAttempts, 0u);
		EXPECT_FALSE(Graph.CancelGraphEdit());
		// Successful cancellation leaves the next edit cycle usable as well.
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.CancelGraphEdit());
		EXPECT_EQ(Provider->mAllocationAttempts, 5u);
	}

	TEST(ArdaInductorLifecycle, FrameRetirementWaitsOnlyItsSubmissionsAndKeepsTheCompletedReceipt)
	{
		auto Provider = eastl::make_shared<FLifecycleProvider>();
		FArdaDependencyFrameTicket::FState State;
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
		auto Provider = eastl::make_shared<FLifecycleProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		auto Profile = eastl::make_shared<FArdaInductorTimingAccumulator>();
		Profile->Configure(.5, 2);
		const FArdaGraphNodeHandle Node(1, 1, 123);
		const eastl::array<EArdaRHIQueueType, 4> Queues = {EArdaRHIQueueType::Compute,
		    EArdaRHIQueueType::Copy,
		    EArdaRHIQueueType::Graphics,
		    EArdaRHIQueueType::Compute};
		eastl::vector<eastl::unique_ptr<FArdaDependencyFrameTicket::FState>> Frames;
		for (uint32_t I = 0; I < Queues.size(); ++I)
		{
			Provider->mNextTimerSeconds = float(1 + 2 * I);
			const auto Timer = Device->CreateTimerQuery();
			ASSERT_TRUE(Timer);
			auto Frame = eastl::make_unique<FArdaDependencyFrameTicket::FState>();
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
		auto Provider = eastl::make_shared<FLifecycleProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		FArdaDependencyFrameTicket::FState First, Second;
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
		auto Provider = eastl::make_shared<FLifecycleProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		{
			FArdaInductorRuntime Runtime;
			auto Frame = eastl::make_unique<FArdaInductorFrame>();
			Frame->mActive = eastl::make_shared<FArdaDependencyFrameTicket::FState>();
			Frame->mActive->mDevice = Device;
			Frame->mActive->mExecution.mLastSubmittedInstances = {31, 0, 0};
			Frame->mActive->mExecution.mStatus = FLifecycleProvider::Unsupported();
			Runtime.mFrames.push_back(eastl::move(Frame));
			ASSERT_TRUE(Runtime.WaitAll());
			EXPECT_FALSE(Runtime.mFrames.front()->mActive->mExecution.mStatus);
		}
		EXPECT_EQ(Provider->mWaitedSubmissions, (eastl::vector<uint64_t>{31}));
		EXPECT_EQ(Provider->mIdleWaitCount, 0u);
	}

	TEST(ArdaInductorLifecycle, AutomaticRetirementPublishesInFrameOrderAfterSlotWraparound)
	{
		auto Provider = eastl::make_shared<FLifecycleProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		auto Output = eastl::make_shared<eastl::vector<uint8_t>>();
		FArdaInductorRuntime Runtime;
		for (const uint64_t Sequence : {4, 2, 3})
		{
			auto Frame = eastl::make_unique<FArdaInductorFrame>();
			Frame->mActive = eastl::make_shared<FArdaDependencyFrameTicket::FState>();
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
		auto Provider = eastl::make_shared<FLifecycleProvider>();
		FArdaDependencyGraph::FImpl Graph;
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
