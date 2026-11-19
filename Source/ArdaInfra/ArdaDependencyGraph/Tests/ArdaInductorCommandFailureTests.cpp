#include "Graph/Node/ArdaNodeDefinition.h"
#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Inductor/Runtime/ArdaInductorCommandExecutor.h"
#include "ArdaDependencyGraph.h"
#include "Inductor/Preprocess/ArdaGraphPreprocessRule.h"
#include "../../ArdaBackend/Private/RHI/Device/ArdaRHIDevicePrivate.h"

#include <gtest/gtest.h>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace arda::inductor_command_failure_tests
{
	using namespace arda;

	struct FArdaRecordingFailure
	{
		eastl::string mOperation;
		uint32_t mFailureCount = 0;
		uint32_t mTimerBeginAttempts = 0;
		uint32_t mWriteBufferAttempts = 0;
		uint32_t mNativeOperationAttempts = 0;
		bool mbThrowNativeOperation = false;
		eastl::shared_ptr<const FArdaProviderNativeOperation> mRetainedNativeOperation;

		FArdaRHIStatus Check(const char* Operation)
		{
			if (std::strcmp(Operation, "WriteBuffer") == 0)
			{
				++mWriteBufferAttempts;
			}
			if (mOperation == Operation && mFailureCount == 0)
			{
				++mFailureCount;
				return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
				    "Injected native command recording failure.");
			}
			return {};
		}
	};

	class FArdaRecordingObject final : public IArdaProviderObject
	{
	public:
		const void* GetIdentity() const noexcept override
		{
			return this;
		}
	};

	class FArdaRecordingTimer final : public IArdaProviderObject
	{
	public:
		enum class EArdaState
		{
			Idle,
			Recording,
			Recorded,
			Complete
		};
		EArdaState mState = EArdaState::Idle;
		const void* mOwner = nullptr;

		const void* GetIdentity() const noexcept override
		{
			return this;
		}
	};

	/** CPU command recorder: unused work is inert, while each barrier can fail independently. */
	class FArdaRecordingCommandList final : public IArdaProviderCommandList
	{
	public:
		explicit FArdaRecordingCommandList(FArdaRecordingFailure& Failure)
		    : mFailure(Failure)
		{
		}

		~FArdaRecordingCommandList() override
		{
			for (const auto& Timer : mTimers)
			{
				if (Timer->mOwner == this)
				{
					Timer->mOwner = nullptr;
					Timer->mState = FArdaRecordingTimer::EArdaState::Idle;
				}
			}
		}

		FArdaRHIStatus RecordNativeOperation(
		    const eastl::shared_ptr<const FArdaProviderNativeOperation>& Packet) override
		{
			++mFailure.mNativeOperationAttempts;
			if (mFailure.mbThrowNativeOperation)
			{
				throw std::runtime_error("Native operation recording threw.");
			}
			auto Status = mFailure.Check("RecordNativeOperation");
			if (Status)
			{
				mFailure.mRetainedNativeOperation = Packet;
			}
			return Status;
		}

		FArdaRHIStatus BeginTimerQuery(const FArdaProviderObjectRef& Object) override
		{
			++mFailure.mTimerBeginAttempts;
			if (auto Status = mFailure.Check("BeginTimerQuery"); !Status)
			{
				return Status;
			}
			auto Timer = eastl::static_pointer_cast<FArdaRecordingTimer>(Object);
			if (Timer->mState != FArdaRecordingTimer::EArdaState::Idle)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Timer is not idle.");
			}
			Timer->mOwner = this;
			Timer->mState = FArdaRecordingTimer::EArdaState::Recording;
			mTimers.push_back(eastl::move(Timer));
			return {};
		}

		FArdaRHIStatus EndTimerQuery(const FArdaProviderObjectRef& Object) override
		{
			if (auto Status = mFailure.Check("EndTimerQuery"); !Status)
			{
				return Status;
			}
			auto* Timer = static_cast<FArdaRecordingTimer*>(Object.get());
			Timer->mState = FArdaRecordingTimer::EArdaState::Recorded;
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
		FArdaRecordingFailure& mFailure;
		bool mbOpen = false;
		eastl::vector<eastl::shared_ptr<FArdaRecordingTimer>> mTimers;
	};

	/** The real facade wraps CPU objects, preserving ownership checks and command-list lifetime behavior. */
	class FArdaRecordingProvider final : public IArdaRHIProviderDevice
	{
	public:
		FArdaRecordingFailure mFailure;
		uint32_t mSubmissionCount = 0;
		uint32_t mWaitCount = 0;
		uint32_t mIdleTimerPolls = 0;
		eastl::vector<eastl::shared_ptr<FArdaRecordingTimer>> mTimers;
		FArdaRHICapabilities mCapabilities;

		FArdaRecordingProvider()
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

		bool SupportsNativeOperation(const char* Id) const noexcept override
		{
			return Id && std::strcmp(Id, "test.native.operation") == 0;
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
			return {eastl::make_shared<FArdaRecordingObject>(), {}};
		}

		FArdaProviderObjectResult CreateBuffer(const FArdaRHIBufferDesc&) override
		{
			return {eastl::make_shared<FArdaRecordingObject>(), {}};
		}

		FArdaProviderObjectResult CreateTimerQuery() override
		{
			auto Timer = eastl::make_shared<FArdaRecordingTimer>();
			mTimers.push_back(Timer);
			return {eastl::move(Timer), {}};
		}

		TArdaRHIResult<bool> PollTimerQuery(const FArdaProviderObjectRef& Object) override
		{
			auto* Timer = static_cast<FArdaRecordingTimer*>(Object.get());
			if (Timer->mState == FArdaRecordingTimer::EArdaState::Idle)
			{
				++mIdleTimerPolls;
			}
			return {Timer->mState == FArdaRecordingTimer::EArdaState::Complete, {}};
		}

		TArdaRHIResult<float> GetTimerQuerySeconds(const FArdaProviderObjectRef& Object) override
		{
			if (static_cast<FArdaRecordingTimer*>(Object.get())->mState != FArdaRecordingTimer::EArdaState::Complete)
			{
				return {0, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Timer is not complete.")};
			}
			return {0.001f, {}};
		}

		FArdaRHIStatus ResetTimerQuery(const FArdaProviderObjectRef& Object) override
		{
			auto* Timer = static_cast<FArdaRecordingTimer*>(Object.get());
			if (Timer->mState != FArdaRecordingTimer::EArdaState::Idle &&
			    Timer->mState != FArdaRecordingTimer::EArdaState::Complete)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Timer remains in use.");
			}
			Timer->mState = FArdaRecordingTimer::EArdaState::Idle;
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
			return {eastl::make_shared<FArdaRecordingObject>(), {}};
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

		TArdaRHIResult<FArdaRHIMemoryRequirements> QueryBufferMemoryRequirements(const FArdaRHIBufferDesc&) override
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
			return {eastl::make_unique<FArdaRecordingCommandList>(mFailure), {}};
		}

		TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList&, EArdaRHIQueueType) override
		{
			if (auto Status = mFailure.Check("ExecuteCommandList"); !Status)
			{
				return {{}, Status};
			}
			for (const auto& Timer : mTimers)
			{
				if (Timer->mState == FArdaRecordingTimer::EArdaState::Recorded)
				{
					Timer->mState = FArdaRecordingTimer::EArdaState::Complete;
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
			return mFailure.Check("WaitForSubmission");
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

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTimedNodeParameters)
		ARDA_NODE_PARAMETER(eastl::shared_ptr<uint32_t>, mBodyCount)
	ARDA_END_NODE_PARAMETER_STRUCT()

	struct FArdaTimedTestNode
	    : TArdaNodeDefinition<FArdaTimedTestNode, FArdaTimedNodeParameters, EArdaNodeType::Graphics>
	{
		using FArdaParameters = FArdaTimedNodeParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.timer.record-body", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(reinterpret_cast<uintptr_t>(P.mBodyCount.get())).Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaTimedNodeParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaTimedNodeParameters& Parameters,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			++*Parameters.mBodyCount;
			return FArdaRHIStatus{};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaTimedTestNode, RecordTimedBody);
	ARDA_DEFINE_GRAPH_NODE(FArdaTimedTestNode, RecordTimedBody);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaRecordingReentryParameters)
		ARDA_NODE_PARAMETER(eastl::shared_ptr<std::function<void()>>, mCallback)
	ARDA_END_NODE_PARAMETER_STRUCT()

	struct FArdaRecordingReentryNode
	    : TArdaNodeDefinition<FArdaRecordingReentryNode, FArdaRecordingReentryParameters, EArdaNodeType::Graphics>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.recording.reentry", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			return FArdaDependencyKeyBuilder{}.Value(reinterpret_cast<uintptr_t>(Parameters.mCallback.get())).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			return Desc;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters& Parameters,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			(*Parameters.mCallback)();
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaRecordingReentryNode, RecordGraphReentry);
	ARDA_DEFINE_GRAPH_NODE(FArdaRecordingReentryNode, RecordGraphReentry);

	FArdaRHIStatus ConfigureTimedGraph(FArdaDependencyGraph& Graph,
	    eastl::shared_ptr<uint32_t> BodyCount,
	    const char* NodeName)
	{
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		Options.mbEnableGpuTiming = true;
		if (auto Status = Edit.SetOptions(Options); !Status)
		{
			return Status;
		}
		auto Node = RecordTimedBody(Edit, FArdaTimedNodeParameters{eastl::move(BodyCount)}, NodeName);
		const auto Unregistered = FArdaNodeRegistry::Get().Unregister(FArdaTimedTestNode::GetMetadata().mName);
		if (!Node)
		{
			return Edit.GetStatus();
		}
		if (!Unregistered)
		{
			return Unregistered;
		}
		if (auto Status = Edit.Commit(); !Status)
		{
			return Status;
		}
		return Graph.CompileAndWait();
	}

	TEST(ArdaInductorCommandFailure, TimerBeginFailureDropsSampleAndAllowsLaterFrameTiming)
	{
		auto Provider = eastl::make_shared<FArdaRecordingProvider>();
		Provider->mFailure.mOperation = "BeginTimerQuery";
		Provider->mCapabilities.mQueues.mGraphicsTimestampValidBits = 64;
		FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
		auto BodyCount = eastl::make_shared<uint32_t>(0);
		ASSERT_TRUE(ConfigureTimedGraph(Graph, BodyCount, "test.timer.begin-failure"));
		FArdaGraphExecuteOptions Options;
		for (uint32_t Frame = 0; Frame != 3; ++Frame)
		{
			auto Submitted = Graph.SubmitAndWait(Options);
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
		auto Provider = eastl::make_shared<FArdaRecordingProvider>();
		Provider->mFailure.mOperation = "EndTimerQuery";
		Provider->mCapabilities.mQueues.mGraphicsTimestampValidBits = 64;
		FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
		auto BodyCount = eastl::make_shared<uint32_t>(0);
		ASSERT_TRUE(ConfigureTimedGraph(Graph, BodyCount, "test.timer.end-failure"));
		FArdaGraphExecuteOptions Options;
		const auto Failed = Graph.ExecuteAndWait(Options);
		EXPECT_EQ(Failed.mStatus.mCode, EArdaRHIResult::BackendFailure);
		EXPECT_STREQ(Failed.mStatus.mMessage.c_str(), "Injected native command recording failure.");
		EXPECT_EQ(Provider->mSubmissionCount, 0u);
		EXPECT_EQ(*BodyCount, 1u);
		EXPECT_TRUE(Graph.GetTimingProfile().empty());
		ASSERT_TRUE(Graph.CompileAndWait(true));
		const auto Recovered = Graph.ExecuteAndWait(Options);
		ASSERT_TRUE(Recovered.mStatus) << Recovered.mStatus.mMessage.c_str();
		const auto Profile = Graph.GetTimingProfile();
		ASSERT_EQ(Profile.size(), 1u);
		EXPECT_EQ(Profile.front().mSampleCount, 1u);
		EXPECT_EQ(*BodyCount, 2u);
		EXPECT_EQ(Provider->mFailure.mFailureCount, 1u);
		EXPECT_EQ(Provider->mIdleTimerPolls, 0u);
	}

	void ExpectRecordingFailure(const FArdaGraphExecutionResult& Result,
	    const FArdaRecordingProvider& Provider,
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
			auto Provider = eastl::make_shared<FArdaRecordingProvider>();
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
			auto Provider = eastl::make_shared<FArdaRecordingProvider>();
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
			auto Provider = eastl::make_shared<FArdaRecordingProvider>();
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
			Options.mbValidateResourceStates = Validate;
			const auto& Result = FArdaInductorCommandExecutor::Submit(Program, Options);
			ExpectRecordingFailure(Result, *Provider, BodyCount);
		}
	}
}

namespace arda
{
	namespace
	{
		class FArdaSubmitDuringPreprocessRule final : public FArdaGraphPreprocessRule
		{
		public:
			FArdaSubmitDuringPreprocessRule(FArdaDependencyGraph& Source, eastl::vector<FArdaRHIStatus>& Results)
			    : mSource(Source),
			      mResults(Results)
			{
			}

			const char* GetName() const noexcept override
			{
				return "SubmitDuringPreprocess";
			}

			FArdaRHIStatus Apply(FArdaInductorGraph&) const override
			{
				mResults.push_back(mSource.SubmitAndWait().mStatus);
				return {};
			}

		private:
			FArdaDependencyGraph& mSource;
			eastl::vector<FArdaRHIStatus>& mResults;
		};

		class FArdaSubmitDuringCompileRule final : public FArdaGraphCompileRule
		{
		public:
			FArdaSubmitDuringCompileRule(FArdaDependencyGraph& Source, eastl::vector<FArdaRHIStatus>& Results)
			    : mSource(Source),
			      mResults(Results)
			{
			}

			const char* GetName() const noexcept override
			{
				return "SubmitDuringCompile";
			}

			FArdaRHIStatus Apply(const FArdaInductorGraph&, FArdaInductorCompileResult&) const override
			{
				mResults.push_back(mSource.SubmitAndWait().mStatus);
				return {};
			}

		private:
			FArdaDependencyGraph& mSource;
			eastl::vector<FArdaRHIStatus>& mResults;
		};

		void ExpectRuleSubmitRejectedBeforeDispatch(bool bPreprocess)
		{
			auto Provider = eastl::make_shared<inductor_command_failure_tests::FArdaRecordingProvider>();
			FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
			eastl::vector<FArdaRHIStatus> Results;
			FArdaInductorOptions Options;
			if (bPreprocess)
			{
				Options.mRules.mPreprocessRules.push_back(
				    MakeArdaGraphRule<FArdaSubmitDuringPreprocessRule>(Graph, Results));
			}
			else
			{
				Options.mRules.mCompileRules.push_back(MakeArdaGraphRule<FArdaSubmitDuringCompileRule>(Graph, Results));
			}
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetOptions(Options));
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			auto Buffer = AllocateBuffer(Edit, Desc);
			Upload(Edit, Buffer, eastl::vector<uint8_t>(16, 7));
			Edit.Keep(Buffer);
			ASSERT_TRUE(Edit.Commit());
			if (bPreprocess)
			{
				const auto Preprocessed = Graph.Preprocess();
				ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
			}
			else
			{
				const auto Compiled = Graph.CompileAndWait();
				ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			}
			ASSERT_EQ(Results.size(), 1u);
			EXPECT_EQ(Results.front().mCode, EArdaRHIResult::InvalidState);
			EXPECT_EQ(Provider->mSubmissionCount, 0u);
		}

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaInitializerReadParameters)
			ARDA_NODE_BUFFER(mInput, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaInitializerReadNode final : public TArdaNodeDefinition<FArdaInitializerReadNode,
		                                           FArdaInitializerReadParameters,
		                                           EArdaNodeType::Graphics>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.initializer.read", 1};
			}

			static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
			{
				FArdaDependencyNodeDesc Desc;
				Desc.mbSideEffect = true;
				return Desc;
			}

			static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
			    const FArdaParameters& Parameters,
			    const FArdaState&,
			    FArdaInstanceState&)
			{
				return Context.GetBuffer(Parameters.mInput)
				    ? FArdaRHIStatus{}
				    : FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Initializer buffer was not prepared.");
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaInitializerReadNode, ReadInitializedFixtureBuffer);
		ARDA_DEFINE_GRAPH_NODE(FArdaInitializerReadNode, ReadInitializedFixtureBuffer);

		FArdaRHIStatus QueueInitializedFixtureGraph(FArdaDependencyGraph& Graph, uint8_t Value)
		{
			FArdaGraphEdit Edit(Graph);
			FArdaInductorOptions Options;
			Options.mFramesInFlight = 2;
			if (auto Status = Edit.SetOptions(Options); !Status)
			{
				return Status;
			}
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			const auto Buffer = AllocateInitializedBuffer(Edit, Desc, eastl::vector<uint8_t>(16, Value), "data");
			ReadInitializedFixtureBuffer(Edit, FArdaInitializerReadParameters{Buffer}, "consume");
			return Edit.Commit();
		}
	}

	TEST(ArdaGraphReentry, SubmitFromPreprocessRuleIsRejectedBeforeDispatch)
	{
		ExpectRuleSubmitRejectedBeforeDispatch(true);
	}

	TEST(ArdaGraphReentry, SubmitFromCompileRuleIsRejectedBeforeDispatch)
	{
		ExpectRuleSubmitRejectedBeforeDispatch(false);
	}

	TEST(ArdaGraphReentry, RecordingCallbacksRejectSameGraphMutationAndPermitStableInspection)
	{
		auto Provider = eastl::make_shared<inductor_command_failure_tests::FArdaRecordingProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		FArdaDependencyGraph Graph(Device);
		FArdaDependencyGraph Independent(Device);
		const auto Caller = std::this_thread::get_id();
		uint32_t CallbackCount = 0;
		FArdaGraphRevisionStats Before;
		auto Callback = eastl::make_shared<std::function<void()>>(
		    [&]
		    {
			    ++CallbackCount;
			    EXPECT_EQ(std::this_thread::get_id(), Caller);
			    FArdaGraphEdit Reentered(Graph);
			    EXPECT_EQ(Reentered.GetStatus().mCode, EArdaRHIResult::InvalidState);
			    EXPECT_FALSE(Reentered.IsOpen());
			    EXPECT_EQ(Graph.CompileAndWait().mCode, EArdaRHIResult::InvalidState);
			    EXPECT_EQ(Graph.Preprocess().mStatus.mCode, EArdaRHIResult::InvalidState);
			    EXPECT_EQ(FArdaInductor::Compile(Graph).mStatus.mCode, EArdaRHIResult::InvalidState);
			    EXPECT_EQ(Graph.DiscardPendingEdits().mCode, EArdaRHIResult::InvalidState);
			    EXPECT_EQ(Graph.InvalidatePersistentCache().mCode, EArdaRHIResult::InvalidState);
			    EXPECT_EQ(Graph.SetRevisionCacheCapacity(2).mCode, EArdaRHIResult::InvalidState);
			    EXPECT_EQ(Graph.DeclareExternalValue<uint32_t>("reentrant", "u32").mCode, EArdaRHIResult::InvalidState);
			    EXPECT_EQ(Graph.Serialize().mStatus.mCode, EArdaRHIResult::InvalidState);
			    EXPECT_FALSE(Graph.IsEditing());
			    const auto During = Graph.GetRevisionStats();
			    EXPECT_EQ(During.mCompileCount, Before.mCompileCount);
			    EXPECT_EQ(During.mPersistentRevision, Before.mPersistentRevision);
			    EXPECT_EQ(During.mConstructionHash, Before.mConstructionHash);
			    EXPECT_EQ(During.mbEditQueueLocked, Before.mbEditQueueLocked);
			    EXPECT_FALSE(Independent.IsEditing());
			    EXPECT_TRUE(QueueInitializedFixtureGraph(Independent, 3));
			    EXPECT_TRUE(Independent.CompileAndWait());
		    });
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(inductor_command_failure_tests::RecordGraphReentry(Edit,
		    inductor_command_failure_tests::FArdaRecordingReentryParameters{Callback},
		    "reentry"));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.CompileAndWait());
		Before = Graph.GetRevisionStats();
		FArdaGraphExecuteOptions Options;
		Options.mbValidateResourceStates = false;
		const auto Result = Graph.ExecuteAndWait(Options);
		ASSERT_TRUE(Result.mStatus) << Result.mStatus.mMessage.c_str();
		EXPECT_EQ(CallbackCount, 1u);
		// Scope restoration must not reject later ordinary authoring on this thread.
		FArdaGraphEdit After(Graph);
		EXPECT_TRUE(After.GetStatus());
		EXPECT_TRUE(After.Cancel());
	}

	class FArdaTestNativeOperation final : public IArdaRHINativeOperation
	{
	public:
		const char* GetId() const noexcept override
		{
			return "test.native.operation";
		}
	};

	TEST(ArdaRHINativeOperation, ForwardsOwnedResolvedViewsAndRetainsPayload)
	{
		auto Provider = eastl::make_shared<inductor_command_failure_tests::FArdaRecordingProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		auto List = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(List);
		EXPECT_TRUE(Device->SupportsNativeOperation("test.native.operation"));
		EXPECT_FALSE(Device->SupportsNativeOperation("other.native.operation"));
		EXPECT_FALSE(Device->SupportsNativeOperation(nullptr));
		EXPECT_FALSE(Device->SupportsNativeOperation(""));
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 32;
		BufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		auto Buffer = Device->CreateBuffer(BufferDesc);
		ASSERT_TRUE(Buffer);
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = TextureDesc.mHeight = 4;
		TextureDesc.mMipLevels = 2;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource;
		auto Texture = Device->CreateTexture(TextureDesc);
		ASSERT_TRUE(Texture);

		FArdaRHINativeOperationDesc Desc;
		Desc.mOperation = eastl::make_shared<FArdaTestNativeOperation>();
		eastl::weak_ptr<const IArdaRHINativeOperation> OperationLifetime(Desc.mOperation);
		Desc.mBindings.push_back({Buffer.mValue, EArdaComputeAccess::ReadWrite, {4, ArdaRHIWholeBuffer}, {}});
		Desc.mBindings.push_back({Texture.mValue, EArdaComputeAccess::Read, {}, {1}});

		EXPECT_EQ(List.mValue->RecordNativeOperation(Desc).mCode, EArdaRHIResult::InvalidState);
		ASSERT_TRUE(List.mValue->Open());
		ASSERT_TRUE(List.mValue->RecordNativeOperation(Desc));
		const auto& Packet = Provider->mFailure.mRetainedNativeOperation;
		ASSERT_TRUE(Packet);
		EXPECT_STREQ(Packet->mOperation->GetId(), "test.native.operation");
		ASSERT_EQ(Packet->mBindings.size(), 2u);
		EXPECT_TRUE(Packet->mBindings[0].mObject);
		EXPECT_EQ(Packet->mBindings[0].mType, EArdaRHIResourceType::Buffer);
		EXPECT_EQ(Packet->mBindings[0].mAccess, EArdaComputeAccess::ReadWrite);
		EXPECT_EQ(Packet->mBindings[0].mBufferRange.mByteOffset, 4u);
		EXPECT_EQ(Packet->mBindings[0].mBufferRange.mByteSize, 28u);
		EXPECT_TRUE(Packet->mBindings[1].mObject);
		EXPECT_EQ(Packet->mBindings[1].mTextureRange.mBaseMipLevel, 1u);
		EXPECT_EQ(Packet->mBindings[1].mTextureRange.mMipLevelCount, 1u);
		ASSERT_TRUE(List.mValue->Close());
		const auto Submitted = Device->ExecuteCommandList(List.mValue);
		ASSERT_TRUE(Submitted);
		Desc = {};
		Buffer.mValue.Reset();
		Texture.mValue.Reset();
		List.mValue.Reset();
		EXPECT_FALSE(OperationLifetime.expired());
		EXPECT_EQ(Device->GetResourceLifetimeStats().GetLiveResourceCount(EArdaRHIResourceType::Buffer), 1u);
		EXPECT_EQ(Device->GetResourceLifetimeStats().GetLiveResourceCount(EArdaRHIResourceType::Texture), 1u);
		ASSERT_TRUE(Device->WaitForSubmission(Submitted.mValue));
		Provider->mFailure.mRetainedNativeOperation.reset();
		EXPECT_TRUE(OperationLifetime.expired());
		EXPECT_EQ(Device->GetResourceLifetimeStats().GetLiveResourceCount(EArdaRHIResourceType::Buffer), 0u);
		EXPECT_EQ(Device->GetResourceLifetimeStats().GetLiveResourceCount(EArdaRHIResourceType::Texture), 0u);
	}

	TEST(ArdaRHINativeOperation, RejectsInvalidViewsAndLatchesTheFirstRecordingError)
	{
		auto Provider = eastl::make_shared<inductor_command_failure_tests::FArdaRecordingProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		auto OtherProvider = eastl::make_shared<inductor_command_failure_tests::FArdaRecordingProvider>();
		auto OtherDevice = CreateArdaRHIDevice(OtherProvider);
		auto List = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(List);
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 32;
		BufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		auto Buffer = Device->CreateBuffer(BufferDesc);
		auto OtherBuffer = OtherDevice->CreateBuffer(BufferDesc);
		ASSERT_TRUE(Buffer);
		ASSERT_TRUE(OtherBuffer);

		FArdaRHINativeOperationDesc Desc;
		Desc.mOperation = eastl::make_shared<FArdaTestNativeOperation>();
		Desc.mBindings.push_back({Buffer.mValue, EArdaComputeAccess::Read, {30, 4}, {}});
		auto CopyList = Device->CreateCommandList(EArdaRHIQueueType::Copy);
		ASSERT_TRUE(CopyList);
		ASSERT_TRUE(CopyList.mValue->Open());
		EXPECT_EQ(CopyList.mValue->RecordNativeOperation(Desc).mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(CopyList.mValue->Close().mCode, EArdaRHIResult::Unsupported);
		ASSERT_TRUE(List.mValue->Open());
		EXPECT_EQ(List.mValue->RecordNativeOperation(Desc).mCode, EArdaRHIResult::InvalidArgument);
		Desc.mBindings[0] = {OtherBuffer.mValue, EArdaComputeAccess::Read, {}, {}};
		EXPECT_EQ(List.mValue->RecordNativeOperation(Desc).mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(List.mValue->Close().mCode, EArdaRHIResult::InvalidArgument);
		ASSERT_TRUE(List.mValue->Open());
		EXPECT_EQ(List.mValue->RecordNativeOperation(Desc).mCode, EArdaRHIResult::WrongDevice);
		EXPECT_EQ(List.mValue->Close().mCode, EArdaRHIResult::WrongDevice);
		EXPECT_EQ(Provider->mFailure.mNativeOperationAttempts, 0u);

		Provider->mFailure.mOperation = "RecordNativeOperation";
		Desc.mBindings[0].mResource = Buffer.mValue;
		ASSERT_TRUE(List.mValue->Open());
		EXPECT_EQ(List.mValue->RecordNativeOperation(Desc).mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(List.mValue->RecordNativeOperation(Desc).mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(List.mValue->Close().mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(Provider->mFailure.mNativeOperationAttempts, 1u);

		Provider->mFailure.mbThrowNativeOperation = true;
		ASSERT_TRUE(List.mValue->Open());
		EXPECT_EQ(List.mValue->RecordNativeOperation(Desc).mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(List.mValue->Close().mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(Device->ExecuteCommandList(List.mValue).mStatus.mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(Provider->mSubmissionCount, 0u);
	}

	TEST(ArdaRHIBufferCopies, CallerProgressDeliversEachCallbackOnceAndAllowsReentry)
	{
		auto Provider = eastl::make_shared<inductor_command_failure_tests::FArdaRecordingProvider>();
		auto Device = CreateArdaRHIDevice(Provider);
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = sizeof(uint32_t);
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		auto Buffer = Device->CreateBuffer(Desc);
		ASSERT_TRUE(Buffer);
		auto Upload = [&](auto Progress)
		{
			auto CommandList = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(CommandList);
			ASSERT_TRUE(CommandList.mValue->Open());
			uint64_t Token = 0;
			uint32_t Calls = 0;
			const uint32_t Value = 7;
			ASSERT_TRUE(CommandList.mValue->CopyBufferHostToDeviceAsync(*Buffer.mValue,
			    &Value,
			    sizeof(Value),
			    [&](FArdaRHIStatus Status)
			    {
				    EXPECT_TRUE(Status);
				    ++Calls;
				    EXPECT_TRUE(Device->PollSubmission(Token));
			    }));
			ASSERT_TRUE(CommandList.mValue->Close());
			const auto Submitted = Device->ExecuteCommandList(CommandList.mValue);
			ASSERT_TRUE(Submitted);
			Token = Submitted.mValue;
			EXPECT_EQ(Calls, 0u);
			Progress(Token);
			EXPECT_EQ(Calls, 1u);
			ASSERT_TRUE(Device->WaitForSubmission(Token));
			EXPECT_EQ(Calls, 1u);
		};
		Upload(
		    [&](uint64_t Token)
		    {
			    EXPECT_TRUE(Device->PollSubmission(Token));
		    });
		Upload(
		    [&](uint64_t)
		    {
			    Device->RunGarbageCollection();
		    });
		Upload(
		    [&](uint64_t)
		    {
			    EXPECT_TRUE(Device->WaitForIdle());
		    });
	}

	TEST(ArdaRHIBufferCopies, CopyFailureReachesEveryCallbackWhenOneThrows)
	{
		for (const char* Operation : {"ExecuteCommandList", "WaitForSubmission"})
		{
			SCOPED_TRACE(Operation);
			auto Provider = eastl::make_shared<inductor_command_failure_tests::FArdaRecordingProvider>();
			Provider->mFailure.mOperation = Operation;
			auto Device = CreateArdaRHIDevice(Provider);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			auto Buffer = Device->CreateBuffer(Desc);
			ASSERT_TRUE(Buffer) << Buffer.mStatus.mMessage.c_str();
			auto CommandList = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(CommandList);
			ASSERT_TRUE(CommandList.mValue->Open());
			const uint32_t Value = 7;
			uint32_t Calls = 0;
			FArdaRHIStatus Completion;
			ASSERT_TRUE(CommandList.mValue->CopyBufferHostToDeviceAsync(*Buffer.mValue,
			    &Value,
			    sizeof(Value),
			    [&](FArdaRHIStatus Status)
			    {
				    EXPECT_EQ(Status.mCode, EArdaRHIResult::BackendFailure);
				    ++Calls;
				    throw std::runtime_error("first completion failed");
			    }));
			ASSERT_TRUE(CommandList.mValue->CopyBufferHostToDeviceAsync(
			    *Buffer.mValue,
			    &Value,
			    sizeof(Value),
			    [&](FArdaRHIStatus Status)
			    {
				    ++Calls;
				    Completion = Status;
			    },
			    sizeof(Value)));
			ASSERT_TRUE(CommandList.mValue->Close());
			const auto Submitted = Device->ExecuteCommandList(CommandList.mValue);
			EXPECT_EQ(static_cast<bool>(Submitted), std::strcmp(Operation, "WaitForSubmission") == 0);
			if (Submitted)
			{
				EXPECT_EQ(Calls, 0u);
				EXPECT_FALSE(Device->WaitForSubmission(Submitted.mValue));
			}
			EXPECT_EQ(Completion.mCode, EArdaRHIResult::BackendFailure);
			EXPECT_EQ(Calls, 2u);
		}
	}

	TEST(ArdaGraphBufferInitializer, ColdPreparationUploadsOnceAcrossFramesAndEquivalentRevisions)
	{
		auto Provider = eastl::make_shared<inductor_command_failure_tests::FArdaRecordingProvider>();
		FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
		ASSERT_TRUE(QueueInitializedFixtureGraph(Graph, 7));
		const auto InitialCompilation = Graph.CompileAndWait();
		ASSERT_TRUE(InitialCompilation) << InitialCompilation.mMessage.c_str();
		EXPECT_EQ(Provider->mFailure.mWriteBufferAttempts, 0u);
		FArdaGraphExecuteOptions Options;
		Options.mbValidateResourceStates = false;
		for (uint32_t Frame = 0; Frame < 4; ++Frame)
		{
			const auto Executed = Graph.ExecuteAndWait(Options);
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		}
		EXPECT_EQ(Provider->mFailure.mWriteBufferAttempts, 1u);
		const auto Revision = Graph.GetCompileResult().mRevision;
		ASSERT_TRUE(QueueInitializedFixtureGraph(Graph, 7));
		ASSERT_TRUE(Graph.CompileAndWait());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
		ASSERT_TRUE(Graph.ExecuteAndWait(Options).mStatus);
		EXPECT_EQ(Provider->mFailure.mWriteBufferAttempts, 1u);

		ASSERT_TRUE(QueueInitializedFixtureGraph(Graph, 9));
		ASSERT_TRUE(Graph.CompileAndWait());
		EXPECT_NE(Graph.GetCompileResult().mRevision, Revision);
		ASSERT_TRUE(Graph.ExecuteAndWait(Options).mStatus);
		EXPECT_EQ(Provider->mFailure.mWriteBufferAttempts, 2u);
	}

	TEST(ArdaGraphBufferInitializer, FailedColdInitializationIsUnpublishedAndRetriesWithFreshStorage)
	{
		for (const char* Operation : {"WriteBuffer", "SetBufferState", "ExecuteCommandList", "WaitForSubmission"})
		{
			SCOPED_TRACE(Operation);
			auto Provider = eastl::make_shared<inductor_command_failure_tests::FArdaRecordingProvider>();
			Provider->mFailure.mOperation = Operation;
			FArdaDependencyGraph Graph(CreateArdaRHIDevice(Provider));
			ASSERT_TRUE(QueueInitializedFixtureGraph(Graph, 5));
			const auto Compiled = Graph.CompileAndWait();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			FArdaGraphExecuteOptions Options;
			Options.mbValidateResourceStates = false;
			const auto Failed = Graph.ExecuteAndWait(Options);
			EXPECT_FALSE(Failed.mStatus);
			EXPECT_EQ(Provider->mFailure.mFailureCount, 1u);
			EXPECT_EQ(Failed.mSubmittedCommandListCount, 0u);
			const auto Recovered = Graph.ExecuteAndWait(Options);
			ASSERT_TRUE(Recovered.mStatus) << Recovered.mStatus.mMessage.c_str();
			EXPECT_EQ(Provider->mFailure.mWriteBufferAttempts, 2u);
			ASSERT_TRUE(Graph.ExecuteAndWait(Options).mStatus);
			EXPECT_EQ(Provider->mFailure.mWriteBufferAttempts, 2u);
		}
	}
}
