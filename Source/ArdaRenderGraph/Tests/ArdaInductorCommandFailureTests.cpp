#include "ArdaInductorCommandExecutor.h"
#include "ArdaDependencyGraph.h"
#include "../../ArdaBackend/Private/RHI/ArdaRHIDevicePrivate.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	struct FRecordingFailure
	{
		eastl::string mOperation;
		uint32_t mFailureCount = 0;
		uint32_t mTimerBeginAttempts = 0;

		FArdaRHIStatus Check(const char* Operation)
		{
			if (mOperation == Operation && mFailureCount == 0)
			{
				++mFailureCount;
				return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
				    "Injected native command recording failure.");
			}
			return {};
		}
	};

	class FRecordingObject final : public IArdaProviderObject
	{
	public:
		const void* GetIdentity() const noexcept override
		{
			return this;
		}
	};

	class FRecordingTimer final : public IArdaProviderObject
	{
	public:
		enum class EState
		{
			Idle,
			Recording,
			Recorded,
			Complete
		};
		EState mState = EState::Idle;
		const void* mOwner = nullptr;

		const void* GetIdentity() const noexcept override
		{
			return this;
		}
	};

	/** CPU command recorder: unused work is inert, while each barrier can fail independently. */
	class FRecordingCommandList final : public IArdaProviderCommandList
	{
	public:
		explicit FRecordingCommandList(FRecordingFailure& Failure)
		    : mFailure(Failure)
		{
		}

		~FRecordingCommandList() override
		{
			for (const auto& Timer : mTimers)
			{
				if (Timer->mOwner == this)
				{
					Timer->mOwner = nullptr;
					Timer->mState = FRecordingTimer::EState::Idle;
				}
			}
		}

		FArdaRHIStatus BeginTimerQuery(const FArdaProviderObjectRef& Object) override
		{
			++mFailure.mTimerBeginAttempts;
			if (auto Status = mFailure.Check("BeginTimerQuery"); !Status)
			{
				return Status;
			}
			auto Timer = eastl::static_pointer_cast<FRecordingTimer>(Object);
			if (Timer->mState != FRecordingTimer::EState::Idle)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Timer is not idle.");
			}
			Timer->mOwner = this;
			Timer->mState = FRecordingTimer::EState::Recording;
			mTimers.push_back(eastl::move(Timer));
			return {};
		}

		FArdaRHIStatus EndTimerQuery(const FArdaProviderObjectRef& Object) override
		{
			if (auto Status = mFailure.Check("EndTimerQuery"); !Status)
			{
				return Status;
			}
			auto* Timer = static_cast<FRecordingTimer*>(Object.get());
			Timer->mState = FRecordingTimer::EState::Recorded;
			return {};
		}

		bool IsOpen() const noexcept override
		{
			return mbOpen;
		}

		FArdaRHIStatus Open() override
		{
			mbOpen = true;
			return {};
		}

		FArdaRHIStatus Close() override
		{
			mbOpen = false;
			return {};
		}

		FArdaRHIStatus Reset() override
		{
			mbOpen = false;
			return {};
		}

#define ARDA_RECORDING_STATUS(Method, ...)                                                                             \
	FArdaRHIStatus Method(__VA_ARGS__) override                                                                        \
	{                                                                                                                  \
		return mFailure.Check(#Method);                                                                                \
	}
		ARDA_RECORDING_STATUS(WriteBuffer,
		    const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&,
		    const void*,
		    size_t,
		    uint64_t)
		ARDA_RECORDING_STATUS(CopyBuffer,
		    const FArdaProviderObjectRef&,
		    uint64_t,
		    const FArdaProviderObjectRef&,
		    uint64_t,
		    uint64_t)
		ARDA_RECORDING_STATUS(CopyTexture,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSlice&,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSlice&)
		ARDA_RECORDING_STATUS(ResolveTexture,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSlice&,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSlice&)
		ARDA_RECORDING_STATUS(CopyTextureToStaging,
		    const FArdaProviderObjectRef&,
		    const FArdaRHIStagingTextureDesc&,
		    const FArdaRHITextureSlice&,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSlice&)
		ARDA_RECORDING_STATUS(CopyTextureFromStaging,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSlice&,
		    const FArdaProviderObjectRef&,
		    const FArdaRHIStagingTextureDesc&,
		    const FArdaRHITextureSlice&)
		ARDA_RECORDING_STATUS(ClearTexture,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSubresourceRange&,
		    const FArdaRHIColor&)
		ARDA_RECORDING_STATUS(ClearTextureUInt,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSubresourceRange&,
		    uint32_t)
		ARDA_RECORDING_STATUS(ClearBufferUInt, const FArdaProviderObjectRef&, const FArdaRHIBufferDesc&, uint32_t)
		ARDA_RECORDING_STATUS(ClearDepthStencilTexture,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSubresourceRange&,
		    bool,
		    float,
		    bool,
		    uint8_t)
		ARDA_RECORDING_STATUS(SetTextureState,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSubresourceRange&,
		    EArdaRHIResourceState)
		ARDA_RECORDING_STATUS(SetBufferState,
		    const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&,
		    EArdaRHIResourceState)
		ARDA_RECORDING_STATUS(TransitionTexture,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureTransitionDesc&)
		ARDA_RECORDING_STATUS(TransitionBuffer,
		    const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&,
		    const FArdaRHIBufferTransitionDesc&)
		ARDA_RECORDING_STATUS(BeginTrackingTextureState,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSubresourceRange&,
		    EArdaRHIResourceState)
		ARDA_RECORDING_STATUS(BeginTrackingBufferState,
		    const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&,
		    EArdaRHIResourceState)
		ARDA_RECORDING_STATUS(SetUAVBarriersForTexture, const FArdaProviderObjectRef&, bool)
		ARDA_RECORDING_STATUS(SetUAVBarriersForBuffer, const FArdaProviderObjectRef&, bool)
		ARDA_RECORDING_STATUS(AliasingBarrier, const FArdaProviderObjectRef&, const FArdaProviderObjectRef&)
		ARDA_RECORDING_STATUS(SetAccelStructState, const FArdaProviderObjectRef&, EArdaRHIResourceState)
		ARDA_RECORDING_STATUS(SetGraphicsState, const FArdaProviderGraphicsState&)
		ARDA_RECORDING_STATUS(SetComputeState, const FArdaProviderComputeState&)
		ARDA_RECORDING_STATUS(DrawIndirect, const FArdaProviderObjectRef&, uint64_t, uint32_t, uint32_t)
		ARDA_RECORDING_STATUS(DrawIndexedIndirect, const FArdaProviderObjectRef&, uint64_t, uint32_t, uint32_t)
		ARDA_RECORDING_STATUS(DispatchIndirect, const FArdaProviderObjectRef&, uint64_t)
#undef ARDA_RECORDING_STATUS

#define ARDA_RECORDING_VOID(Method, ...)                                                                               \
	void Method(__VA_ARGS__) override                                                                                  \
	{                                                                                                                  \
	}
		ARDA_RECORDING_VOID(SetAutomaticBarriers, bool)
		ARDA_RECORDING_VOID(CommitBarriers)
		ARDA_RECORDING_VOID(SetPushConstants, const void*, size_t)
		ARDA_RECORDING_VOID(Draw, const FArdaRHIDrawArguments&)
		ARDA_RECORDING_VOID(DrawIndexed, const FArdaRHIDrawArguments&)
		ARDA_RECORDING_VOID(Dispatch, uint32_t, uint32_t, uint32_t)
		ARDA_RECORDING_VOID(BeginMarker, const char*)
		ARDA_RECORDING_VOID(EndMarker)
#undef ARDA_RECORDING_VOID

		TArdaRHIResult<FArdaRHINativeResourceState> QueryTextureState(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSubresourceRange&) const override
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "State validation is disabled in this fixture.")};
		}

		TArdaRHIResult<FArdaRHINativeResourceState> QueryBufferState(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&) const override
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "State validation is disabled in this fixture.")};
		}

	private:
		FRecordingFailure& mFailure;
		bool mbOpen = false;
		eastl::vector<eastl::shared_ptr<FRecordingTimer>> mTimers;
	};

	/** The real facade wraps CPU objects, preserving ownership checks and command-list lifetime behavior. */
	class FRecordingProvider final : public IArdaRHIProviderDevice
	{
	public:
		FRecordingFailure mFailure;
		uint32_t mSubmissionCount = 0;
		uint32_t mWaitCount = 0;
		uint32_t mIdleTimerPolls = 0;
		eastl::vector<eastl::shared_ptr<FRecordingTimer>> mTimers;
		FArdaRHICapabilities mCapabilities;

		FRecordingProvider()
		{
			mCapabilities.mRayTracing.mbAccelerationStructures = true;
		}

		static FArdaRHIStatus Unsupported()
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Not used by the CPU command fixture.");
		}

		const FArdaRHICapabilities& GetCapabilities() const noexcept override
		{
			return mCapabilities;
		}

		EArdaRHINativeResourceType GetTextureImportType() const noexcept override
		{
			return EArdaRHINativeResourceType::BackendDefined;
		}

		EArdaRHINativeResourceType GetBufferImportType() const noexcept override
		{
			return EArdaRHINativeResourceType::BackendDefined;
		}

		FArdaProviderObjectResult CreateTexture(const FArdaRHITextureDesc&) override
		{
			return {eastl::make_shared<FRecordingObject>(), {}};
		}

		FArdaProviderObjectResult CreateBuffer(const FArdaRHIBufferDesc&) override
		{
			return {eastl::make_shared<FRecordingObject>(), {}};
		}

		FArdaProviderObjectResult CreateTimerQuery() override
		{
			auto Timer = eastl::make_shared<FRecordingTimer>();
			mTimers.push_back(Timer);
			return {eastl::move(Timer), {}};
		}

		TArdaRHIResult<bool> PollTimerQuery(const FArdaProviderObjectRef& Object) override
		{
			auto* Timer = static_cast<FRecordingTimer*>(Object.get());
			if (Timer->mState == FRecordingTimer::EState::Idle)
			{
				++mIdleTimerPolls;
			}
			return {Timer->mState == FRecordingTimer::EState::Complete, {}};
		}

		TArdaRHIResult<float> GetTimerQuerySeconds(const FArdaProviderObjectRef& Object) override
		{
			if (static_cast<FRecordingTimer*>(Object.get())->mState != FRecordingTimer::EState::Complete)
			{
				return {0, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Timer is not complete.")};
			}
			return {0.001f, {}};
		}

		FArdaRHIStatus ResetTimerQuery(const FArdaProviderObjectRef& Object) override
		{
			auto* Timer = static_cast<FRecordingTimer*>(Object.get());
			if (Timer->mState != FRecordingTimer::EState::Idle && Timer->mState != FRecordingTimer::EState::Complete)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Timer remains in use.");
			}
			Timer->mState = FRecordingTimer::EState::Idle;
			return {};
		}

		TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements> GetAccelStructBuildMemoryRequirements(
		    const FArdaRHIAccelStructDesc&,
		    const eastl::vector<FArdaProviderRayTracingGeometry>&) override
		{
			return {{256, 256, 0, 256, 256}, {}};
		}

		FArdaProviderObjectResult CreateAccelStruct(const FArdaRHIAccelStructDesc&,
		    const FArdaRHIAccelStructMemoryRequirements&) override
		{
			return {eastl::make_shared<FRecordingObject>(), {}};
		}

		uint64_t GetAccelStructDeviceAddress(const FArdaProviderObjectRef&) const noexcept override
		{
			return 256;
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&) override
		{
			return {{256, 256, 1}, {}};
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&) override
		{
			return {{256, 256, 1}, {}};
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

#define ARDA_RECORDING_UNSUPPORTED_OBJECT(Method, Parameter)                                                           \
	FArdaProviderObjectResult Method(const Parameter&) override                                                        \
	{                                                                                                                  \
		return {{}, Unsupported()};                                                                                    \
	}
		ARDA_RECORDING_UNSUPPORTED_OBJECT(CreateHeap, FArdaRHIHeapDesc)
		ARDA_RECORDING_UNSUPPORTED_OBJECT(CreateStagingTexture, FArdaRHIStagingTextureDesc)
		ARDA_RECORDING_UNSUPPORTED_OBJECT(ImportTexture, FArdaRHINativeTextureImportDesc)
		ARDA_RECORDING_UNSUPPORTED_OBJECT(ImportBuffer, FArdaRHINativeBufferImportDesc)
		ARDA_RECORDING_UNSUPPORTED_OBJECT(CreateSampler, FArdaRHISamplerDesc)
		ARDA_RECORDING_UNSUPPORTED_OBJECT(CreateShader, FArdaRHIShaderDesc)
		ARDA_RECORDING_UNSUPPORTED_OBJECT(CreateBindingLayout, FArdaRHIBindingLayoutDesc)
		ARDA_RECORDING_UNSUPPORTED_OBJECT(CreateFramebuffer, FArdaProviderFramebufferCreateInfo)
		ARDA_RECORDING_UNSUPPORTED_OBJECT(CreateGraphicsPipeline, FArdaProviderGraphicsPipelineCreateInfo)
		ARDA_RECORDING_UNSUPPORTED_OBJECT(CreateComputePipeline, FArdaProviderComputePipelineCreateInfo)
#undef ARDA_RECORDING_UNSUPPORTED_OBJECT

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
			return {eastl::make_unique<FRecordingCommandList>(mFailure), {}};
		}

		TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList&, EArdaRHIQueueType) override
		{
			for (const auto& Timer : mTimers)
			{
				if (Timer->mState == FRecordingTimer::EState::Recorded)
				{
					Timer->mState = FRecordingTimer::EState::Complete;
					Timer->mOwner = nullptr;
				}
			}
			return {++mSubmissionCount, {}};
		}

		TArdaRHIResult<bool> PollSubmission(uint64_t) override
		{
			return {true, {}};
		}

		FArdaRHIStatus WaitForSubmission(uint64_t) override
		{
			++mWaitCount;
			return {};
		}

		FArdaRHIStatus WaitForIdle() override
		{
			return {};
		}

		void RunGarbageCollection() override
		{
		}

		void FlushPipelineCache() noexcept override
		{
		}
	};

	struct FTimedNodeParameters
	{
		eastl::shared_ptr<uint32_t> mBodyCount;
	};

	FArdaRHIStatus ConfigureTimedGraph(FArdaDependencyGraph& Graph,
	    eastl::shared_ptr<uint32_t> BodyCount,
	    const char* DefinitionName)
	{
		TArdaDependencyNodeDefinition<FTimedNodeParameters> Definition;
		Definition.mName = DefinitionName;
		Definition.mKind = EArdaDependencyNodeKind::Graphics;
		Definition.mCanonicalKey = [](const FTimedNodeParameters&)
		{
			return eastl::string("timed-body");
		};
		Definition.mDescribe = [](const FTimedNodeParameters&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			return Desc;
		};
		Definition.mRecord = [](FArdaDependencyExecutionContext&, const FTimedNodeParameters& Parameters)
		{
			++*Parameters.mBodyCount;
			return FArdaRHIStatus{};
		};
		if (auto Status = FArdaNodeRegistry::Get().Register(eastl::move(Definition)); !Status)
		{
			return Status;
		}
		if (auto Status = Graph.BeginGraphEdit(); !Status)
		{
			return Status;
		}
		FArdaInductorOptions Options;
		Options.mbEnableGpuTiming = true;
		if (auto Status = Graph.SetOptions(Options); !Status)
		{
			return Status;
		}
		auto Node = Graph.AttachOrFind("timed node", DefinitionName, FTimedNodeParameters{eastl::move(BodyCount)});
		const auto Unregistered = FArdaNodeRegistry::Get().Unregister(DefinitionName);
		if (!Node)
		{
			return Node.mStatus;
		}
		if (!Unregistered)
		{
			return Unregistered;
		}
		return Graph.EndGraphEdit();
	}

	TEST(ArdaInductorCommandFailure, TimerBeginFailureDropsSampleAndAllowsLaterFrameTiming)
	{
		auto Provider = eastl::make_shared<FRecordingProvider>();
		Provider->mFailure.mOperation = "BeginTimerQuery";
		Provider->mCapabilities.mQueues.mGraphicsTimestampValidBits = 64;
		FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
		auto BodyCount = eastl::make_shared<uint32_t>(0);
		ASSERT_TRUE(ConfigureTimedGraph(Graph, BodyCount, "test.timer.begin-failure"));
		FArdaGraphExecuteOptions Options;
		Options.mbParallelRecording = false;
		for (uint32_t Frame = 0; Frame != 3; ++Frame)
		{
			auto Submitted = Graph.Submit(Options);
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			const auto WaitCount = Provider->mWaitCount;
			Graph.CollectTelemetry();
			const auto Profile = Graph.GetTimingProfile();
			EXPECT_EQ(Provider->mWaitCount, WaitCount);
			EXPECT_EQ(Profile.size(), Frame ? 1u : 0u);
			if (!Profile.empty())
			{
				EXPECT_EQ(Profile.front().mSampleCount, Frame);
			}
		}
		EXPECT_EQ(*BodyCount, 3u);
		EXPECT_EQ(Provider->mFailure.mTimerBeginAttempts, 3u);
		EXPECT_EQ(Provider->mFailure.mFailureCount, 1u);
		EXPECT_EQ(Provider->mIdleTimerPolls, 0u);
	}

	TEST(ArdaInductorCommandFailure, TimerEndFailureRejectsUnsafeRecordingAndRecoversAfterEdit)
	{
		auto Provider = eastl::make_shared<FRecordingProvider>();
		Provider->mFailure.mOperation = "EndTimerQuery";
		Provider->mCapabilities.mQueues.mGraphicsTimestampValidBits = 64;
		FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
		auto BodyCount = eastl::make_shared<uint32_t>(0);
		ASSERT_TRUE(ConfigureTimedGraph(Graph, BodyCount, "test.timer.end-failure"));
		FArdaGraphExecuteOptions Options;
		Options.mbParallelRecording = false;
		const auto Failed = Graph.Execute(Options);
		EXPECT_EQ(Failed.mStatus.mCode, EArdaRHIResult::BackendFailure);
		EXPECT_STREQ(Failed.mStatus.mMessage.c_str(), "Injected native command recording failure.");
		EXPECT_EQ(Provider->mSubmissionCount, 0u);
		EXPECT_EQ(*BodyCount, 1u);
		EXPECT_TRUE(Graph.GetTimingProfile().empty());
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto Recovered = Graph.Execute(Options);
		ASSERT_TRUE(Recovered.mStatus) << Recovered.mStatus.mMessage.c_str();
		const auto Profile = Graph.GetTimingProfile();
		ASSERT_EQ(Profile.size(), 1u);
		EXPECT_EQ(Profile.front().mSampleCount, 1u);
		EXPECT_EQ(*BodyCount, 2u);
		EXPECT_EQ(Provider->mFailure.mFailureCount, 1u);
		EXPECT_EQ(Provider->mIdleTimerPolls, 0u);
	}

	void ExpectRecordingFailure(const FArdaGraphExecutionResult& Result,
	    const FRecordingProvider& Provider,
	    uint32_t BodyCount)
	{
		EXPECT_EQ(Result.mStatus.mCode, EArdaRHIResult::BackendFailure);
		EXPECT_STREQ(Result.mStatus.mMessage.c_str(), "Injected native command recording failure.");
		EXPECT_EQ(Provider.mFailure.mFailureCount, 1u);
		EXPECT_EQ(BodyCount, 0u);
		EXPECT_EQ(Provider.mSubmissionCount, 0u);
		EXPECT_EQ(Result.mSubmittedCommandListCount, 0u);
		for (const uint64_t Instance : Result.mLastSubmittedInstances)
		{
			EXPECT_EQ(Instance, 0u);
		}
	}

	TEST(ArdaInductorCommandFailure, BufferBarrierFailuresStopRecordingWithoutStateValidation)
	{
		for (const char* Operation : {"BeginTrackingBufferState", "SetUAVBarriersForBuffer", "SetBufferState"})
		{
			SCOPED_TRACE(Operation);
			auto Provider = eastl::make_shared<FRecordingProvider>();
			Provider->mFailure.mOperation = Operation;
			auto Device = CreateArdaRHIDevice(Provider);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 256;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			auto Native = Device->CreateBuffer(Desc);
			ASSERT_TRUE(Native) << Native.mStatus.mMessage.c_str();
			FArdaInductorCommandProgram Program(Device);
			auto* Buffer = Program.BindBuffer(Native.mValue, EArdaRHIResourceState::Common, "buffer");
			FArdaInductorCommandAccesses Accesses;
			Accesses.mBuffers.push_back({Buffer->GetHandle(), {}, EArdaRHIResourceState::UnorderedAccess, true});
			uint32_t BodyCount = 0;
			Program.AppendCommand("buffer writer",
			    EArdaRHIQueueType::Graphics,
			    eastl::move(Accesses),
			    [&](FArdaInductorPassContext&)
			    {
				    ++BodyCount;
			    });
			Program.Finalize();
			FArdaGraphExecuteOptions Options;
			Options.mbParallelRecording = false;
			Options.mbValidateResourceStates = false;
			const auto& Result = FArdaInductorCommandExecutor::Submit(Program, Options);
			ExpectRecordingFailure(Result, *Provider, BodyCount);
		}
	}

	TEST(ArdaInductorCommandFailure, TextureBarrierFailuresStopRecordingWithoutStateValidation)
	{
		for (const char* Operation : {"BeginTrackingTextureState", "SetUAVBarriersForTexture", "SetTextureState"})
		{
			SCOPED_TRACE(Operation);
			auto Provider = eastl::make_shared<FRecordingProvider>();
			Provider->mFailure.mOperation = Operation;
			auto Device = CreateArdaRHIDevice(Provider);
			FArdaRHITextureDesc Desc;
			Desc.mWidth = Desc.mHeight = 4;
			Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
			Desc.mUsage = EArdaRHITextureUsage::UnorderedAccess;
			auto Native = Device->CreateTexture(Desc);
			ASSERT_TRUE(Native) << Native.mStatus.mMessage.c_str();
			FArdaInductorCommandProgram Program(Device);
			auto* Texture = Program.BindTexture(Native.mValue, EArdaRHIResourceState::Common, "texture");
			FArdaInductorCommandAccesses Accesses;
			Accesses.mTextures.push_back({Texture->GetHandle(), {}, EArdaRHIResourceState::UnorderedAccess, true});
			uint32_t BodyCount = 0;
			Program.AppendCommand("texture writer",
			    EArdaRHIQueueType::Graphics,
			    eastl::move(Accesses),
			    [&](FArdaInductorPassContext&)
			    {
				    ++BodyCount;
			    });
			Program.Finalize();
			FArdaGraphExecuteOptions Options;
			Options.mbParallelRecording = false;
			Options.mbValidateResourceStates = false;
			const auto& Result = FArdaInductorCommandExecutor::Submit(Program, Options);
			ExpectRecordingFailure(Result, *Provider, BodyCount);
		}
	}

	TEST(ArdaInductorCommandFailure, AccelerationStructureStateFailureStopsRecording)
	{
		for (const bool Validate : {false, true})
		{
			SCOPED_TRACE(Validate);
			auto Provider = eastl::make_shared<FRecordingProvider>();
			Provider->mFailure.mOperation = "SetAccelStructState";
			auto Device = CreateArdaRHIDevice(Provider);
			FArdaRHIAccelStructDesc Desc;
			Desc.mbTopLevel = true;
			Desc.mTopLevelMaxInstances = 1;
			auto Native = Device->CreateAccelStruct(Desc);
			ASSERT_TRUE(Native) << Native.mStatus.mMessage.c_str();
			FArdaInductorCommandProgram Program(Device);
			auto* AccelStruct =
			    Program.BindAccelerationStructure(Native.mValue, EArdaRHIResourceState::AccelStructRead, "AS");
			FArdaInductorCommandAccesses Accesses;
			Accesses.mAccelerationStructures.push_back(
			    {AccelStruct->GetHandle(), EArdaRHIResourceState::AccelStructWrite, true});
			uint32_t BodyCount = 0;
			Program.AppendCommand("AS build",
			    EArdaRHIQueueType::Graphics,
			    eastl::move(Accesses),
			    [&](FArdaInductorPassContext&)
			    {
				    ++BodyCount;
			    });
			Program.Finalize();
			FArdaGraphExecuteOptions Options;
			Options.mbParallelRecording = false;
			Options.mbValidateResourceStates = Validate;
			const auto& Result = FArdaInductorCommandExecutor::Submit(Program, Options);
			ExpectRecordingFailure(Result, *Provider, BodyCount);
		}
	}
}
