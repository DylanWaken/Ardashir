#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "ArdaDependencyGraph.h"
#include "Graph/DefaultNodes/ArdaDependencyGraphNodes.h"
#include "Graph/Node/ArdaDependencyNode.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"
#include "RHI/Shaders/ArdaShaderCompiler.h"

#include <gtest/gtest.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace arda::inductor_gpu_tests
{
	using namespace arda;

	class FArdaInductorGpuDiagnostics final : public IArdaDiagnosticCallback
	{
	public:
		void Message(EArdaDiagnosticSeverity Severity, const char* Text) override
		{
			if (Severity == EArdaDiagnosticSeverity::Error || Severity == EArdaDiagnosticSeverity::Fatal)
			{
				++mErrors;
				std::fprintf(stderr, "Inductor validation: %s\n", Text ? Text : "");
			}
		}

		std::atomic<uint32_t> mErrors{0};
	};

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaCompiledComputeBindings)
		ARDA_SHADER_BUFFER_UAV(mFirst, 2, 3, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_BUFFER_UAV(mSecond, 5, 0, EArdaRHIShaderStage::Compute)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	struct FArdaInductorPipelineTrace
	{
		eastl::vector<FArdaRHIComputePipelineRef> mPipelines;
		eastl::vector<uint64_t> mKeys;
		eastl::vector<eastl::vector<FArdaRHIBindingSetRef>> mBindings;
	};

	struct FArdaInductorAutoComputeParameters
	{
		FArdaDependencyResourceHandle mFirst;
		FArdaDependencyResourceHandle mSecond;
		FArdaRHIShaderRef mShader;
		eastl::vector<FArdaRHIBindingLayoutRef> mLayouts;
		uint32_t mCount = 16;
		bool mbStageOnly = false;
		eastl::shared_ptr<FArdaInductorPipelineTrace> mTrace;
	};

	struct FArdaAutoComputeTestNode : TArdaDependencyNode<FArdaAutoComputeTestNode,
	                                      FArdaInductorAutoComputeParameters,
	                                      EArdaDependencyNodeKind::Compute>
	{
		using FArdaParameters = FArdaInductorAutoComputeParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.inductor.auto-compute-pipeline", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaInductorAutoComputeParameters& P)
		{

			FArdaDependencyKeyBuilder Key;
			Key.Resource(P.mFirst)
			    .Resource(P.mSecond)
			    .Value(P.mShader ? P.mShader->GetPersistentCacheHash() : 0)
			    .Value(P.mCount)
			    .Value(P.mbStageOnly)
			    .Value(reinterpret_cast<uintptr_t>(P.mTrace.get()));

			// Shader layout semantics participate in identity independently of native object addresses.
			Key.Value(P.mLayouts.size());
			for (const auto& Layout : P.mLayouts)
			{
				Key.Value(Layout ? HashValue(Layout->GetDesc()) : 0);
			}
			return Key.Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaInductorAutoComputeParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;

			// Contribute the shader independently from the node that consumes its pipeline.
			if (P.mShader)
			{
				FArdaInductorPipelineContribution Contribution;
				Contribution.mShader = P.mShader;
				Contribution.mBindingLayouts = P.mLayouts;
				Desc.mPipelineStages.push_back(eastl::move(Contribution));
			}
			Desc.mbPipelineStageOnly = P.mbStageOnly;
			if (P.mbStageOnly)
			{
				return Desc;
			}

			// Let compilation derive bindings and connect the executable node to its pipeline slot.
			Desc.BindShader<FArdaCompiledComputeBindings>({{"mFirst", P.mFirst}, {"mSecond", P.mSecond}});
			FArdaInductorPipelineRequest Request;
			Request.mSlot = "default";
			Request.mKind = EArdaPipelineStateKind::Compute;
			Desc.mPipelines.push_back(eastl::move(Request));
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaInductorAutoComputeParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			if (P.mbStageOnly)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "A stage-only declaration must not become a native recording pass.");
			}
			const auto* Pipeline = Context.GetPipeline();
			if (!Pipeline || !Pipeline->mCompute)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "The compiler did not resolve the compute pipeline slot.");
			}

			// Retain the prepared objects so replay can verify pipeline and binding reuse.
			P.mTrace->mPipelines.push_back(Pipeline->mCompute);
			P.mTrace->mKeys.push_back(Pipeline->mStableKey);
			P.mTrace->mBindings.push_back(Context.GetBindings());

			// Consume graph-owned bindings and record the actual dispatch.
			if (auto S = Context.SetComputeState(); !S)
			{
				return S;
			}
			Context.GetCommands().Dispatch(P.mCount, 1, 1);
			return FArdaRHIStatus{};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaAutoComputeTestNode, RunAutoCompute);
	ARDA_DEFINE_GRAPH_NODE(FArdaAutoComputeTestNode, RunAutoCompute);

	class FArdaInductorGpu : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			if (!FindBackendModule(GetParam()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			FArdaBackendConfiguration Configuration = arda::MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mbEnableValidation = arda::ArdaTestValidationEnabled;
			Configuration.mMessageCallback = &mDiagnostics;
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(Configuration)) << GetBackendError().c_str();
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
		}

		void TearDown() override
		{
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice->RunGarbageCollection();
				mDevice.Reset();
			}
			ShutdownBackend();
			EXPECT_EQ(mDiagnostics.mErrors.load(), 0u);
		}

		FArdaRHIShaderRef CreateConformanceShader(const char* Entry = "RegisteredCS",
		    EArdaRHIShaderStage Stage = EArdaRHIShaderStage::Compute,
		    const char* File = "ArdaGraphConformance.hlsl")
		{
			const auto Path = (std::filesystem::path(ARDA_RDG_TEST_SHADER_SOURCE_DIR) / File).string();
			FArdaShaderTypeRegistration Registration(Entry, Path.c_str(), Entry, Entry, Stage, nullptr);
			const auto Previous = GetShaderCompilerConfiguration();
			auto Configuration = Previous;
			Configuration.mbCompileMissingArtifacts = true;
			Configuration.mbCompileOutdatedArtifacts = true;
			ConfigureShaderCompiler(Configuration);
			const auto Compiled = EnsureRegisteredShaderArtifact(Registration.GetType(),
			    GetParam(),
			    0,
			    std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR));
			ConfigureShaderCompiler(Previous);
			if (!Compiled || Compiled.mJobs.empty())
			{
				ADD_FAILURE() << (Compiled.mDiagnostics.empty() ? "No shader compilation job"
				                                                : Compiled.mDiagnostics.front().mMessage.c_str());
				return {};
			}
			std::ifstream Input(Compiled.mJobs.front().mOutputPath, std::ios::binary | std::ios::ate);
			if (!Input)
			{
				return {};
			}
			eastl::vector<uint8_t> Bytecode(static_cast<size_t>(Input.tellg()));
			Input.seekg(0);
			Input.read(reinterpret_cast<char*>(Bytecode.data()), Bytecode.size());
			FArdaRHIShaderDesc Desc;
			Desc.mStage = Stage;
			Desc.mEntryPoint = Entry;
			Desc.mBytecode = Bytecode.data();
			Desc.mBytecodeSize = Bytecode.size();
			auto Shader = mDevice->CreateShader(Desc);
			EXPECT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
			return Shader.mValue;
		}

		FArdaInductorGpuDiagnostics mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(FArdaInductorGpu, CreatesComputePipelineOnFirstExecutionAndReusesItAcrossFrames)
	{
		uint32_t DirectSubmittedLists = 0;
		for (bool bStageAncestry : {false, true})
		{
			SCOPED_TRACE(bStageAncestry ? "stage -> sync -> consumer" : "direct stage contribution");
			FArdaInductorAutoComputeParameters Parameters;
			Parameters.mShader = CreateConformanceShader();
			ASSERT_TRUE(Parameters.mShader);
			for (uint32_t Space : {3u, 0u})
			{
				FArdaRHIBindingLayoutDesc Desc;
				Desc.mVisibility = EArdaRHIShaderStage::Compute;
				Desc.mRegisterSpace = Space;
				Desc.mbRegisterSpaceIsDescriptorSet = true;
				Desc.mItems.push_back({Space == 3 ? 2u : 5u, 1, EArdaRHIBindingType::StructuredBufferUAV});
				auto Created = mDevice->CreateBindingLayout(Desc);
				ASSERT_TRUE(Created);
				Parameters.mLayouts.push_back(eastl::move(Created.mValue));
			}
			Parameters.mTrace = eastl::make_shared<FArdaInductorPipelineTrace>();
			auto FirstBytes = eastl::make_shared<eastl::vector<uint8_t>>();
			auto SecondBytes = eastl::make_shared<eastl::vector<uint8_t>>();
			FArdaDependencyGraph Graph(mDevice);
			FArdaGraphEdit Edit(Graph);
			FArdaInductorOptions Options;
			Options.mFramesInFlight = 1;
			Options.mbEnableAsyncCompute = false;
			Options.mbEnableCopyQueue = false;
			ASSERT_TRUE(Edit.SetOptions(Options));
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = Parameters.mCount * 4;
			Desc.mStructureStride = 4;
			Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
			const auto First = AllocateBuffer(Edit, Desc, "First compute output");
			const auto Second = AllocateBuffer(Edit, Desc, "Second compute output");
			ASSERT_TRUE(First);
			ASSERT_TRUE(Second);
			Parameters.mFirst = First.GetHandle();
			Parameters.mSecond = Second.GetHandle();
			// Consumers are attached before the producer; resource semantics must determine order.
			const auto FirstRead =
			    ReadbackNode(Edit, FArdaGraphReadbackParameters{First.GetHandle(), FirstBytes}, "Read first");
			const auto SecondRead =
			    ReadbackNode(Edit, FArdaGraphReadbackParameters{Second.GetHandle(), SecondBytes}, "Read second");
			ASSERT_TRUE(FirstRead);
			ASSERT_TRUE(SecondRead);
			auto StageParameters = Parameters;
			StageParameters.mbStageOnly = true;
			StageParameters.mFirst = {};
			StageParameters.mSecond = {};
			if (bStageAncestry)
			{
				Parameters.mShader.Reset();
				Parameters.mLayouts.clear();
			}
			const auto Produce = RunAutoCompute(Edit, Parameters, "Run inferred compute");
			ASSERT_TRUE(Produce);
			FArdaGraphNodeHandle Stage, Sync;
			if (bStageAncestry)
			{
				const auto SyncResult = SyncNode(Edit, FArdaGraphSyncParameters{}, "Stage ancestry join");
				const auto StageResult = RunAutoCompute(Edit, StageParameters, "Compute stage declaration");
				ASSERT_TRUE(SyncResult);
				ASSERT_TRUE(StageResult);
				Stage = StageResult.GetHandle();
				Sync = SyncResult.GetHandle();
				ASSERT_TRUE(Edit.Order(StageResult, SyncResult));
				ASSERT_TRUE(Edit.Order(SyncResult, Produce));
			}
			const auto Before = Graph.GetPipelineCacheStats();
			ASSERT_TRUE(Edit.Commit());
			const auto Compiled = Graph.Compile();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			ASSERT_TRUE(Graph.GetCompileResult().mStatus);
			uint64_t Revision = Graph.GetCompileResult().mRevision;
			EXPECT_GT(Revision, 0u);
			const auto CompiledCache = Graph.GetPipelineCacheStats();
			EXPECT_EQ(CompiledCache.mComputeEntries, Before.mComputeEntries);
			EXPECT_EQ(CompiledCache.mMisses, Before.mMisses);
			EXPECT_EQ(CompiledCache.mHits, Before.mHits);
			EXPECT_EQ(CompiledCache.mCreateFailures, 0u);
			EXPECT_TRUE(Parameters.mTrace->mPipelines.empty()); // Compilation publishes only the logical plan.
			const auto& Order = Graph.GetCompileResult().mExecutionOrder;
			ASSERT_EQ(Order.size(), bStageAncestry ? 5u : 3u);
			if (bStageAncestry)
			{
				EXPECT_EQ(Order[0], Stage);
				EXPECT_EQ(Order[1], Sync);
				EXPECT_EQ(Order[2], Produce.GetHandle());
			}
			else
			{
				EXPECT_EQ(Order.front(), Produce.GetHandle());
			}
			for (uint32_t Frame = 0; Frame < 4; ++Frame)
			{
				SCOPED_TRACE(Frame);
				if (Frame == 3)
				{
					const auto Recompiled = Graph.Compile(true);
					ASSERT_TRUE(Recompiled) << Recompiled.mMessage.c_str();
					EXPECT_GT(Graph.GetCompileResult().mRevision, Revision);
					Revision = Graph.GetCompileResult().mRevision;
					const auto RecompiledCache = Graph.GetPipelineCacheStats();
					EXPECT_EQ(RecompiledCache.mComputeEntries, CompiledCache.mComputeEntries + 1);
					EXPECT_EQ(RecompiledCache.mMisses, CompiledCache.mMisses + 1);
					EXPECT_EQ(RecompiledCache.mHits, CompiledCache.mHits);
					EXPECT_EQ(RecompiledCache.mCreateFailures, 0u);
				}
				FirstBytes->assign(Desc.mByteSize, 0xff);
				SecondBytes->assign(Desc.mByteSize, 0xff);
				const auto Execution = Graph.Execute();
				ASSERT_TRUE(Execution.mStatus) << Execution.mStatus.mMessage.c_str();
				ASSERT_TRUE(mDevice->WaitForIdle());
				EXPECT_EQ(Execution.mStateConformanceFailureCount, 0u);
				if (!bStageAncestry && Frame == 0)
				{
					DirectSubmittedLists = Execution.mSubmittedCommandListCount;
					EXPECT_GT(DirectSubmittedLists, 0u);
				}
				if (bStageAncestry)
				{
					// Shader declarations and pure synchronization do not submit extra native lists.
					EXPECT_EQ(Execution.mSubmittedCommandListCount, DirectSubmittedLists);
				}
				EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
				ASSERT_EQ(Parameters.mTrace->mPipelines.size(), Frame + 1);
				EXPECT_EQ(Parameters.mTrace->mPipelines.back(), Parameters.mTrace->mPipelines.front());
				EXPECT_EQ(Parameters.mTrace->mKeys.back(), Parameters.mTrace->mKeys.front());
				ASSERT_EQ(Parameters.mTrace->mBindings.back().size(), 2u);
				if (Frame < 3)
				{
					EXPECT_EQ(Parameters.mTrace->mBindings.back(), Parameters.mTrace->mBindings.front());
				}
				const auto FrameCache = Graph.GetPipelineCacheStats();
				EXPECT_EQ(FrameCache.mComputeEntries, CompiledCache.mComputeEntries + 1);
				EXPECT_EQ(FrameCache.mMisses, CompiledCache.mMisses + 1);
				// First execution creates the PSO; execution after recompilation reuses the cached PSO.
				EXPECT_EQ(FrameCache.mHits, CompiledCache.mHits + (Frame == 3 ? 1u : 0u));
				EXPECT_EQ(FrameCache.mCreateFailures, 0u);
				ASSERT_EQ(FirstBytes->size(), Desc.mByteSize);
				ASSERT_EQ(SecondBytes->size(), Desc.mByteSize);
				for (uint32_t Index = 0; Index < Parameters.mCount; ++Index)
				{
					uint32_t A = 0, B = 0;
					std::memcpy(&A, FirstBytes->data() + Index * 4, 4);
					std::memcpy(&B, SecondBytes->data() + Index * 4, 4);
					EXPECT_EQ(A, 0xA2DA0000u + Index);
					EXPECT_EQ(B, (0xA2DA0000u + Index) ^ 0x12345678u);
				}
			}
		}
	}

	struct FArdaInductorFrameTrace
	{
		eastl::vector<uint32_t> mSlots;
		eastl::vector<const void*> mBuffers;
		eastl::vector<eastl::shared_ptr<eastl::vector<uint8_t>>> mOutputs;
	};

	struct FArdaInductorFrameParameters
	{
		FArdaDependencyResourceHandle mResource;
		eastl::shared_ptr<FArdaInductorFrameTrace> mTrace;
	};

	struct FArdaFrameWriteNode
	    : TArdaDependencyNode<FArdaFrameWriteNode, FArdaInductorFrameParameters, EArdaDependencyNodeKind::Compute>
	{
		using FArdaParameters = FArdaInductorFrameParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.inductor.frame-write", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}
			    .Resource(P.mResource)
			    .Value(reinterpret_cast<uintptr_t>(P.mTrace.get()))
			    .Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			FArdaDependencyAccess Access;
			Access.mResource = P.mResource;
			Access.mAccess = EArdaDependencyAccess::Write;
			Access.mState = EArdaRHIResourceState::UnorderedAccess;
			Desc.mAccesses.push_back(Access);
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			const auto Buffer = C.GetBuffer(P.mResource);
			P.mTrace->mSlots.push_back(C.GetFrameIndex());
			P.mTrace->mBuffers.push_back(Buffer.Get());
			return C.GetCommands().ClearBufferUInt(*Buffer, static_cast<uint32_t>(C.GetFrameSequence()));
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaFrameWriteNode, WriteFrame);
	ARDA_DEFINE_GRAPH_NODE(FArdaFrameWriteNode, WriteFrame);

	struct FArdaFrameReadNode
	    : TArdaDependencyNode<FArdaFrameReadNode, FArdaInductorFrameParameters, EArdaDependencyNodeKind::Copy>
	{
		using FArdaParameters = FArdaInductorFrameParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.inductor.frame-read", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaFrameWriteNode::GetCanonicalKey(P);
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			FArdaDependencyAccess Access;
			Access.mResource = P.mResource;
			Access.mAccess = EArdaDependencyAccess::Read;
			Access.mState = EArdaRHIResourceState::CopySource;
			Desc.mAccesses.push_back(Access);
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return C.ReadbackBuffer(P.mResource, P.mTrace->mOutputs[C.GetFrameSequence() - 1]);
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaFrameReadNode, ReadFrame);
	ARDA_DEFINE_GRAPH_NODE(FArdaFrameReadNode, ReadFrame);

	TEST_P(FArdaInductorGpu, ThreeFrameSlotsPreserveReadbacksAndTicketsAcrossRecyclingAndEditing)
	{
		// Register the independent write/read operations used by every frame-slot scenario.

		struct FArdaUnregister
		{
			~FArdaUnregister()
			{
				(void)FArdaNodeRegistry::Get().Unregister("test.inductor.frame-read");
				(void)FArdaNodeRegistry::Get().Unregister("test.inductor.frame-write");
			}
		} Unregister;

		// Exercise both resource lifetime policies through submission, recycling and graph edits.
		for (bool Persistent : {false, true})
		{
			SCOPED_TRACE(Persistent);
			FArdaDependencyGraph Graph(mDevice);
			FArdaGraphEdit Edit(Graph);
			FArdaInductorOptions Options;
			Options.mFramesInFlight = 3;
			Options.mbEnableGpuTiming = true;
			ASSERT_TRUE(Edit.SetOptions(Options));
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 64;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto Resource = AllocateBuffer(Edit,
			    Desc,
			    "frame storage",
			    Persistent ? EArdaGraphResourceLifetime::Persistent : EArdaGraphResourceLifetime::Transient);
			ASSERT_TRUE(Resource);
			auto Trace = eastl::make_shared<FArdaInductorFrameTrace>();
			for (uint32_t I = 0; I < 4; ++I)
			{
				Trace->mOutputs.push_back(eastl::make_shared<eastl::vector<uint8_t>>());
			}
			const FArdaInductorFrameParameters Parameters{Resource.GetHandle(), Trace};
			ASSERT_TRUE(WriteFrame(Edit, Parameters, "frame writer"));
			ASSERT_TRUE(ReadFrame(Edit, Parameters, "frame reader"));
			ASSERT_TRUE(Edit.Commit());
			const auto Compiled = Graph.Compile();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			const auto Revision = Graph.GetCompileResult().mRevision;
			eastl::vector<FArdaDependencyFrameTicket> Tickets;
			for (uint32_t I = 0; I < 3; ++I)
			{
				auto Submitted = Graph.Submit();
				ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
				Tickets.push_back(Submitted.mValue);
				EXPECT_TRUE(Trace->mOutputs[I]->empty());
				EXPECT_TRUE(Graph.IsComplete(Tickets.back()).mStatus);
			}
			EXPECT_EQ(Trace->mSlots, (eastl::vector<uint32_t>{0, 1, 2}));
			if (Persistent)
			{
				EXPECT_EQ(Trace->mBuffers[0], Trace->mBuffers[1]);
				EXPECT_EQ(Trace->mBuffers[1], Trace->mBuffers[2]);
			}
			else
			{
				EXPECT_NE(Trace->mBuffers[0], Trace->mBuffers[1]);
				EXPECT_NE(Trace->mBuffers[1], Trace->mBuffers[2]);
			}
			auto Recycled = Graph.Submit();
			ASSERT_TRUE(Recycled) << Recycled.mStatus.mMessage.c_str();
			Tickets.push_back(Recycled.mValue);
			ASSERT_EQ(Trace->mSlots.back(), 0u);
			EXPECT_EQ(Trace->mBuffers.back(), Trace->mBuffers.front());
			EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
			for (uint32_t I = 0; I < Tickets.size(); ++I)
			{
				const auto Completed = Graph.Wait(Tickets[I]);
				ASSERT_TRUE(Completed.mStatus) << Completed.mStatus.mMessage.c_str();
				EXPECT_EQ(Completed.mStateConformanceFailureCount, 0u);
				ASSERT_EQ(Trace->mOutputs[I]->size(), 64u);
				for (uint32_t Offset = 0; Offset < 64; Offset += 4)
				{
					uint32_t Value = 0;
					std::memcpy(&Value, Trace->mOutputs[I]->data() + Offset, 4);
					EXPECT_EQ(Value, I + 1);
				}
			}
			const auto Profile = Graph.GetTimingProfile();
			ASSERT_FALSE(Profile.empty());
			EXPECT_EQ(Profile.front().mSampleCount, 4u);
			EXPECT_GT(Profile.front().mGpuSeconds, 0.0);
			const auto Optimized = Graph.OptimizeFromTimingProfile();
			ASSERT_TRUE(Optimized) << Optimized.mMessage.c_str();
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (Graph.GetAdaptiveSchedulingStats().mbPending && std::chrono::steady_clock::now() < Deadline)
			{
				Graph.CollectTelemetry();
				std::this_thread::yield();
			}
			EXPECT_FALSE(Graph.GetAdaptiveSchedulingStats().mbPending);
			EXPECT_EQ(Graph.GetAdaptiveSchedulingStats().mIterations, 1u);
			EXPECT_TRUE(Graph.GetAdaptiveSchedulingStats().mLastStatus);
			EXPECT_FALSE(Graph.GetTimingProfile().empty());
			Trace->mOutputs.push_back(eastl::make_shared<eastl::vector<uint8_t>>());
			const auto AfterOptimization = Graph.Execute();
			ASSERT_TRUE(AfterOptimization.mStatus) << AfterOptimization.mStatus.mMessage.c_str();
			ASSERT_EQ(Trace->mOutputs.back()->size(), 64u);
			uint32_t OptimizedValue = 0;
			std::memcpy(&OptimizedValue, Trace->mOutputs.back()->data(), 4);
			EXPECT_EQ(OptimizedValue, 5u);
			ASSERT_TRUE(Graph.Compile(true));
			EXPECT_TRUE(Graph.GetTimingProfile().empty());
			EXPECT_TRUE(Graph.Wait(Tickets.front()).mStatus);
			EXPECT_TRUE(Graph.IsComplete(Tickets.front()).mValue);
		}
	}

	TEST_P(FArdaInductorGpu, InsertionOrderedPartialOverwritePreservesEarlierReaderAcrossFrames)
	{
		constexpr uint32_t WordCount = 16;
		constexpr uint32_t FirstValue = 0x1100u;
		constexpr uint32_t OverwriteValue = 0xcc00u;
		for (bool bAsyncQueues : {false, true})
		{
			SCOPED_TRACE(bAsyncQueues);
			FArdaDependencyGraph Graph(mDevice);
			FArdaGraphEdit Edit(Graph);
			FArdaInductorOptions Options;
			Options.mFramesInFlight = 2;
			Options.mbEnableAsyncCompute = bAsyncQueues;
			Options.mbEnableCopyQueue = bAsyncQueues;
			ASSERT_TRUE(Edit.SetOptions(Options));
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = WordCount * sizeof(uint32_t);
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto Resource = AllocateBuffer(Edit, Desc, "multi-writer storage");
			ASSERT_TRUE(Resource);
			auto BeforeBytes = eastl::make_shared<eastl::vector<uint8_t>>();
			auto AfterBytes = eastl::make_shared<eastl::vector<uint8_t>>();
			const auto FirstWrite = ClearNode(Edit, {Resource.GetHandle(), FirstValue}, "A first write");
			const auto FirstRead = ReadbackNode(Edit, {Resource.GetHandle(), BeforeBytes}, "B read first contents");
			FArdaGraphUploadParameters Overwrite;
			Overwrite.mDestination = Resource.GetHandle();
			Overwrite.mOffset = Desc.mByteSize / 2;
			Overwrite.mBytes.resize(Desc.mByteSize / 2);
			for (uint32_t Index = 0; Index < WordCount / 2; ++Index)
			{
				const uint32_t Value = OverwriteValue + Index;
				std::memcpy(Overwrite.mBytes.data() + Index * sizeof(Value), &Value, sizeof(Value));
			}
			const auto SecondWrite = UploadNode(Edit, Overwrite, "C overwrite upper half");
			const auto SecondRead = ReadbackNode(Edit, {Resource.GetHandle(), AfterBytes}, "D read final contents");
			ASSERT_TRUE(FirstWrite);
			ASSERT_TRUE(FirstRead);
			ASSERT_TRUE(SecondWrite);
			ASSERT_TRUE(SecondRead);
			// No explicit edges: the overlapping writers select insertion order for this byte range.
			ASSERT_TRUE(Edit.Commit());
			const auto Compiled = Graph.Compile();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
			    (eastl::vector<FArdaGraphNodeHandle>{FirstWrite.GetHandle(),
			        FirstRead.GetHandle(),
			        SecondWrite.GetHandle(),
			        SecondRead.GetHandle()}));
			for (uint32_t Frame = 0; Frame < 5; ++Frame)
			{
				SCOPED_TRACE(Frame);
				BeforeBytes->assign(Desc.mByteSize, 0xff);
				AfterBytes->assign(Desc.mByteSize, 0xff);
				const auto Executed = Graph.Execute();
				ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
				EXPECT_EQ(Executed.mStateConformanceFailureCount, 0u);
				ASSERT_EQ(BeforeBytes->size(), Desc.mByteSize);
				ASSERT_EQ(AfterBytes->size(), Desc.mByteSize);
				for (uint32_t Index = 0; Index < WordCount; ++Index)
				{
					uint32_t Before = 0;
					uint32_t After = 0;
					std::memcpy(&Before, BeforeBytes->data() + Index * sizeof(Before), sizeof(Before));
					std::memcpy(&After, AfterBytes->data() + Index * sizeof(After), sizeof(After));
					EXPECT_EQ(Before, FirstValue) << "B at word " << Index;
					const uint32_t Expected =
					    Index < WordCount / 2 ? FirstValue : OverwriteValue + Index - WordCount / 2;
					EXPECT_EQ(After, Expected) << "D at word " << Index;
				}
			}
		}
	}

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaMultiWriterReadWriteBindings)
		ARDA_SHADER_BUFFER_UAV(mResource, 2, 3, EArdaRHIShaderStage::Compute)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	struct FArdaMultiWriterReadWriteParameters
	{
		FArdaDependencyResourceHandle mResource;
		FArdaRHIShaderRef mShader;
		uint32_t mWordCount = 16;
	};

	struct FArdaMultiWriterReadWriteNode
	    : TArdaComputeDependencyNode<FArdaMultiWriterReadWriteNode, FArdaMultiWriterReadWriteParameters>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.inductor.multi-writer-read-write", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			return FArdaDependencyKeyBuilder{}
			    .Resource(Parameters.mResource)
			    .Value(Parameters.mShader ? Parameters.mShader->GetPersistentCacheHash() : 0)
			    .Value(Parameters.mWordCount)
			    .Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelineStages.push_back({Parameters.mShader});
			Desc.mPipelines.push_back({"default", 0, EArdaPipelineStateKind::Compute});
			FArdaDependencyShaderResource Binding;
			Binding.mMember = "mResource";
			Binding.mResource = Parameters.mResource;
			Binding.mAccess = EArdaDependencyAccess::ReadWrite;
			Desc.BindShader<FArdaMultiWriterReadWriteBindings>({Binding});
			return Desc;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			if (const auto Status = Context.SetComputeState(); !Status)
			{
				return Status;
			}
			Context.GetCommands().Dispatch(Parameters.mWordCount, 1, 1);
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaMultiWriterReadWriteNode, UpdateMultiWriter);
	ARDA_DEFINE_GRAPH_NODE(FArdaMultiWriterReadWriteNode, UpdateMultiWriter);

	TEST_P(FArdaInductorGpu, InsertionOrderedReadWritePreservesEarlierReaderAcrossFrames)
	{
		const auto Shader = CreateConformanceShader("MultiWriterReadWriteCS");
		ASSERT_TRUE(Shader);
		constexpr uint32_t WordCount = 16;
		constexpr uint32_t FirstValue = 0x22da00u;
		for (bool bAsyncQueues : {false, true})
		{
			SCOPED_TRACE(bAsyncQueues);
			FArdaDependencyGraph Graph(mDevice);
			FArdaGraphEdit Edit(Graph);
			FArdaInductorOptions Options;
			Options.mFramesInFlight = 2;
			Options.mbEnableAsyncCompute = bAsyncQueues;
			Options.mbEnableCopyQueue = bAsyncQueues;
			ASSERT_TRUE(Edit.SetOptions(Options));
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = WordCount * sizeof(uint32_t);
			Desc.mStructureStride = sizeof(uint32_t);
			Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
			const auto Resource = AllocateBuffer(Edit, Desc, "read-write storage");
			ASSERT_TRUE(Resource);
			auto BeforeBytes = eastl::make_shared<eastl::vector<uint8_t>>();
			auto AfterBytes = eastl::make_shared<eastl::vector<uint8_t>>();
			const auto FirstWrite = ClearNode(Edit, {Resource.GetHandle(), FirstValue}, "A first write");
			const auto FirstRead = ReadbackNode(Edit, {Resource.GetHandle(), BeforeBytes}, "B read first contents");
			const auto SecondWrite =
			    UpdateMultiWriter(Edit, {Resource.GetHandle(), Shader, WordCount}, "C read-modify-write");
			const auto SecondRead = ReadbackNode(Edit, {Resource.GetHandle(), AfterBytes}, "D read final contents");
			ASSERT_TRUE(FirstWrite);
			ASSERT_TRUE(FirstRead);
			ASSERT_TRUE(SecondWrite);
			ASSERT_TRUE(SecondRead);
			ASSERT_TRUE(Edit.Commit());
			const auto Compiled = Graph.Compile();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
			    (eastl::vector<FArdaGraphNodeHandle>{FirstWrite.GetHandle(),
			        FirstRead.GetHandle(),
			        SecondWrite.GetHandle(),
			        SecondRead.GetHandle()}));
			for (uint32_t Frame = 0; Frame < 5; ++Frame)
			{
				SCOPED_TRACE(Frame);
				BeforeBytes->assign(Desc.mByteSize, 0xff);
				AfterBytes->assign(Desc.mByteSize, 0xff);
				const auto Executed = Graph.Execute();
				ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
				EXPECT_EQ(Executed.mStateConformanceFailureCount, 0u);
				ASSERT_EQ(BeforeBytes->size(), Desc.mByteSize);
				ASSERT_EQ(AfterBytes->size(), Desc.mByteSize);
				for (uint32_t Index = 0; Index < WordCount; ++Index)
				{
					uint32_t Before = 0;
					uint32_t After = 0;
					std::memcpy(&Before, BeforeBytes->data() + Index * sizeof(Before), sizeof(Before));
					std::memcpy(&After, AfterBytes->data() + Index * sizeof(After), sizeof(After));
					EXPECT_EQ(Before, FirstValue) << "B at word " << Index;
					EXPECT_EQ(After, FirstValue + 17u + Index) << "D at word " << Index;
				}
			}
		}
	}

	TEST_P(FArdaInductorGpu, ConsecutiveUAVWritesPreserveReadModifyWriteContentsAcrossFrames)
	{
		const auto Shader = CreateConformanceShader("MultiWriterReadWriteCS");
		ASSERT_TRUE(Shader);
		constexpr uint32_t WordCount = 4096;
		constexpr uint32_t FirstValue = 0x35ad00u;
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		Options.mFramesInFlight = 2;
		Options.mbEnableAsyncCompute = false;
		Options.mbEnableCopyQueue = false;
		ASSERT_TRUE(Edit.SetOptions(Options));
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = WordCount * sizeof(uint32_t);
		Desc.mStructureStride = sizeof(uint32_t);
		Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
		const auto Resource = AllocateBuffer(Edit, Desc, "consecutive UAV storage");
		ASSERT_TRUE(Resource);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		const auto FirstWrite = ClearNode(Edit, {Resource.GetHandle(), FirstValue}, "A first UAV write");
		const auto Update =
		    UpdateMultiWriter(Edit, {Resource.GetHandle(), Shader, WordCount}, "C UAV read-modify-write");
		const auto Read = ReadbackNode(Edit, {Resource.GetHandle(), Bytes}, "D read final contents");
		ASSERT_TRUE(FirstWrite);
		ASSERT_TRUE(Update);
		ASSERT_TRUE(Read);
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{FirstWrite.GetHandle(), Update.GetHandle(), Read.GetHandle()}));
		for (uint32_t Frame = 0; Frame < 5; ++Frame)
		{
			SCOPED_TRACE(Frame);
			Bytes->assign(Desc.mByteSize, 0xff);
			const auto Executed = Graph.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(Executed.mStateConformanceFailureCount, 0u);
			// Both operations use the same queue and state, so their ordering needs a UAV barrier.
			for (const auto Checkpoint : {EArdaGraphStateCheckpoint::BeforeTransition,
			         EArdaGraphStateCheckpoint::AfterTransition,
			         EArdaGraphStateCheckpoint::AfterPass})
			{
				EXPECT_TRUE(eastl::any_of(Executed.mStateConformanceRecords.begin(),
				    Executed.mStateConformanceRecords.end(),
				    [&](const auto& Record)
				    {
					    return Record.mPassName == "C UAV read-modify-write" && Record.mCheckpoint == Checkpoint &&
					        Record.mExpectedState == EArdaRHIResourceState::UnorderedAccess && Record.IsConsistent();
				    }));
			}
			ASSERT_EQ(Bytes->size(), Desc.mByteSize);
			for (uint32_t Index = 0; Index < WordCount; ++Index)
			{
				uint32_t Value = 0;
				std::memcpy(&Value, Bytes->data() + Index * sizeof(Value), sizeof(Value));
				ASSERT_EQ(Value, FirstValue + 17u + Index) << "word " << Index;
			}
		}
	}

	struct FArdaInductorImageAliasParameters
	{
		FArdaDependencyResourceHandle mTexture, mBuffer;
		uint32_t mValue = 0;
	};

	struct FArdaImageAliasClearNode : TArdaDependencyNode<FArdaImageAliasClearNode,
	                                      FArdaInductorImageAliasParameters,
	                                      EArdaDependencyNodeKind::Compute>
	{
		using FArdaParameters = FArdaInductorImageAliasParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.inductor.alias-image-clear", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mTexture).Resource(P.mBuffer).Value(P.mValue).Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mAccesses.push_back(
			    {P.mTexture, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return C.GetCommands().ClearTextureUInt(*C.GetTexture(P.mTexture), {}, P.mValue);
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaImageAliasClearNode, ClearAliasImage);
	ARDA_DEFINE_GRAPH_NODE(FArdaImageAliasClearNode, ClearAliasImage);

	struct FArdaImageAliasCopyNode
	    : TArdaDependencyNode<FArdaImageAliasCopyNode, FArdaInductorImageAliasParameters, EArdaDependencyNodeKind::Copy>
	{
		using FArdaParameters = FArdaInductorImageAliasParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.inductor.alias-image-copy", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaImageAliasClearNode::GetCanonicalKey(P);
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mAccesses.push_back({P.mTexture, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource});
			Desc.mAccesses.push_back({P.mBuffer, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest});
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return C.GetCommands().CopyTextureToBuffer(*C.GetBuffer(P.mBuffer),
			    {0, 1024},
			    *C.GetTexture(P.mTexture),
			    {});
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaImageAliasCopyNode, CopyAliasImage);
	ARDA_DEFINE_GRAPH_NODE(FArdaImageAliasCopyNode, CopyAliasImage);

	TEST_P(FArdaInductorGpu, OverlappingPlacedImagesReactivateAcrossQueuesAndRepeatedFrames)
	{

		// Keep image writes and transfers as separate compiler-visible operations.

		struct FArdaUnregister
		{
			~FArdaUnregister()
			{
				(void)FArdaNodeRegistry::Get().Unregister("test.inductor.alias-image-copy");
				(void)FArdaNodeRegistry::Get().Unregister("test.inductor.alias-image-clear");
			}
		} Unregister;

		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Memory;
		Options.mFramesInFlight = 2;
		ASSERT_TRUE(Edit.SetOptions(Options));
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = TextureDesc.mHeight = 256;
		TextureDesc.mFormat = EArdaRHIFormat::R32UInt;
		TextureDesc.mUsage = EArdaRHITextureUsage::UnorderedAccess;
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 256 * 1024;
		eastl::array<eastl::shared_ptr<eastl::vector<uint8_t>>, 2> Outputs;
		FArdaGraphOperation PreviousCopy;
		for (uint32_t I = 0; I < 2; ++I)
		{
			const eastl::string Suffix = std::to_string(I).c_str();
			const auto Texture = AllocateTexture(Edit, TextureDesc, "image " + Suffix);
			const auto Buffer = AllocateBuffer(Edit, BufferDesc, "capture " + Suffix);
			ASSERT_TRUE(Texture);
			ASSERT_TRUE(Buffer);
			const FArdaInductorImageAliasParameters P{Texture.GetHandle(), Buffer.GetHandle(), 100 + I};
			const auto Writer = ClearAliasImage(Edit, P, "clear image " + Suffix);
			const auto Copied = CopyAliasImage(Edit, P, "capture image " + Suffix);
			ASSERT_TRUE(Writer);
			ASSERT_TRUE(Copied);
			if (PreviousCopy)
			{
				ASSERT_TRUE(Edit.Order(PreviousCopy, Writer));
			}
			PreviousCopy = Copied;
			Outputs[I] = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(ReadbackNode(Edit,
			    FArdaGraphReadbackParameters{Buffer.GetHandle(), Outputs[I]},
			    "read image " + Suffix));
		}
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		for (uint32_t Frame = 0; Frame < 4; ++Frame)
		{
			SCOPED_TRACE(Frame);
			const auto Completed = Graph.Execute();
			ASSERT_TRUE(Completed.mStatus) << Completed.mStatus.mMessage.c_str();
			EXPECT_EQ(Completed.mStateConformanceFailureCount, 0u);
			EXPECT_GE(Completed.mAliasingBarrierCount, 2u);
			EXPECT_TRUE(eastl::any_of(Completed.mStateConformanceRecords.begin(),
			    Completed.mStateConformanceRecords.end(),
			    [](const auto& Record)
			    {
				    return Record.mPassName.find("Activate image ") == 0;
			    }));
			for (uint32_t I = 0; I < Outputs.size(); ++I)
			{
				ASSERT_EQ(Outputs[I]->size(), BufferDesc.mByteSize);
				for (uint32_t Offset = 0; Offset < BufferDesc.mByteSize; Offset += 4)
				{
					uint32_t Value = 0;
					std::memcpy(&Value, Outputs[I]->data() + Offset, 4);
					if (Value != 100 + I)
					{
						ADD_FAILURE() << "Image " << I << " differs at byte " << Offset << ": " << Value;
						break;
					}
				}
			}
		}
	}

	struct FArdaInductorFailureParameters
	{
		FArdaDependencyResourceHandle mOutput, mUndeclared;
		bool mbFail = false;
	};

	struct FArdaFailureRecordingNode : TArdaDependencyNode<FArdaFailureRecordingNode,
	                                       FArdaInductorFailureParameters,
	                                       EArdaDependencyNodeKind::Compute>
	{
		using FArdaParameters = FArdaInductorFailureParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.inductor.failure", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mOutput).Resource(P.mUndeclared).Value(P.mbFail).Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mAccesses.push_back({P.mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
			return D;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			if (P.mUndeclared)
			{
				EXPECT_FALSE(C.GetBuffer(P.mUndeclared));
				// The context must preserve failure even when a callback ignores the null result.
				return FArdaRHIStatus{};
			}
			auto Status = C.GetCommands().ClearBufferUInt(*C.GetBuffer(P.mOutput), 91);
			if (!Status)
			{
				return Status;
			}
			return P.mbFail ? FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "injected record failure") : Status;
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaFailureRecordingNode, RecordFailure);
	ARDA_DEFINE_GRAPH_NODE(FArdaFailureRecordingNode, RecordFailure);

	TEST_P(FArdaInductorGpu, RecordingFailureAndUndeclaredAccessPreventSubmissionAndPermitRepair)
	{
		// The same node class injects recording failure or undeclared resource access.

		struct FArdaUnregister
		{
			~FArdaUnregister()
			{
				(void)FArdaNodeRegistry::Get().Unregister("test.inductor.failure");
			}
		} Unregister;

		for (bool Undeclared : {false, true})
		{
			SCOPED_TRACE(Undeclared);
			FArdaDependencyGraph G(mDevice);
			FArdaGraphEdit Edit(G);
			FArdaRHIBufferDesc D;
			D.mByteSize = 64;
			D.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto Output = AllocateBuffer(Edit, D, "output").GetHandle();
			const auto Hidden = AllocateBuffer(Edit, D, "hidden").GetHandle();
			auto Writer = RecordFailure(Edit,
			    FArdaInductorFailureParameters{Output,
			        Undeclared ? Hidden : FArdaDependencyResourceHandle{},
			        !Undeclared},
			    "writer");
			ASSERT_TRUE(Writer);
			auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(ReadbackNode(Edit, FArdaGraphReadbackParameters{Output, Bytes}, "read"));
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(G.Compile());
			const auto Failed = G.Execute();
			EXPECT_FALSE(Failed.mStatus);
			EXPECT_EQ(Failed.mSubmittedCommandListCount, 0u);
			EXPECT_TRUE(Bytes->empty());
			FArdaGraphEdit Repair(G);
			const auto RepairedOutput = AllocateBuffer(Repair, D, "output");
			ASSERT_TRUE(RecordFailure(Repair, FArdaInductorFailureParameters{RepairedOutput, {}, false}, "writer"));
			ASSERT_TRUE(ReadbackNode(Repair, FArdaGraphReadbackParameters{RepairedOutput, Bytes}, "read"));
			ASSERT_TRUE(Repair.Commit());
			ASSERT_TRUE(G.Compile());
			const auto Repaired = G.Execute();
			ASSERT_TRUE(Repaired.mStatus) << Repaired.mStatus.mMessage.c_str();
			EXPECT_EQ(Repaired.mStateConformanceFailureCount, 0u);
			ASSERT_EQ(Bytes->size(), 64u);
			for (size_t Offset = 0; Offset < Bytes->size(); Offset += 4)
			{
				uint32_t Value = 0;
				std::memcpy(&Value, Bytes->data() + Offset, 4);
				EXPECT_EQ(Value, 91u);
			}
		}
	}

	TEST_P(FArdaInductorGpu, PreservesCurrentImportedTextureStateAndIgnoresUnusedUninitializedImports)
	{
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = TextureDesc.mHeight = 4;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource;
		auto Unused = mDevice->CreateTexture(TextureDesc);
		ASSERT_TRUE(Unused) << Unused.mStatus.mMessage.c_str();
		TextureDesc.mInitialState = EArdaRHIResourceState::Common;
		auto Used = mDevice->CreateTexture(TextureDesc);
		ASSERT_TRUE(Used) << Used.mStatus.mMessage.c_str();
		auto Setup = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Setup);
		ASSERT_TRUE(Setup.mValue->Open());
		ASSERT_TRUE(Setup.mValue->SetTextureState(*Used.mValue, {}, EArdaRHIResourceState::CopySource));
		Setup.mValue->CommitBarriers();
		ASSERT_TRUE(Setup.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Setup.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());

		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(BindTexture(Edit, Unused.mValue, "unused uninitialized"));
		auto Texture = BindTexture(Edit, Used.mValue, "current state differs from creation");
		ASSERT_TRUE(Texture);
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 1024;
		auto Buffer = AllocateBuffer(Edit, BufferDesc, "pitched upload");
		ASSERT_TRUE(Buffer);
		FArdaGraphUploadParameters Upload;
		Upload.mDestination = Buffer.GetHandle();
		Upload.mBytes.resize(1024, 0x7F);
		ASSERT_TRUE(UploadNode(Edit, Upload, "upload"));
		ASSERT_TRUE(BufferToTexture(Edit, {Texture.GetHandle(), {}, Buffer.GetHandle(), {0, 256}}, "copy"));
		Edit.Keep(Texture.GetHandle());
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			const auto Executed = Graph.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(Executed.mStateConformanceFailureCount, 0u);
			ASSERT_TRUE(mDevice->WaitForIdle());
			auto Query = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Query);
			const auto Snapshot = Query.mValue->QueryTextureState(*Used.mValue, {});
			ASSERT_TRUE(Snapshot);
			EXPECT_TRUE(Snapshot.mValue.IsConsistent());
			EXPECT_EQ(Snapshot.mValue.mFacadeState, EArdaRHIResourceState::CopySource);
		}
	}

	struct FArdaBindlessPush
	{
		uint32_t mIndex = 0, mValue = 0;
	};
	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaBindlessShaderParameters)
		ARDA_SHADER_PUSH_CONSTANTS(FArdaBindlessPush, mSelection, 0, 1, EArdaRHIShaderStage::Compute)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	struct FArdaBindlessTestParameters
	{
		FArdaDependencyResourceHandle mOutput;
		FArdaRHIShaderRef mShader;
		uint32_t mIndex = 0, mOffset = 0;
		bool mbDuplicate = false;
		eastl::shared_ptr<eastl::vector<FArdaRHIDescriptorTableRef>> mTables;
	};

	struct FArdaBindlessTestNode : TArdaComputeDependencyNode<FArdaBindlessTestNode, FArdaBindlessTestParameters>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.automatic-bindless", 1};
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
		{
			FArdaRHIBufferDesc D;
			D.mByteSize = 64;
			D.mStructureStride = 4;
			D.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
			return C.Buffer(P.mOutput, "Output", D);
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}
			    .Resource(P.mOutput)
			    .Value(P.mShader ? P.mShader->GetPersistentCacheHash() : 0)
			    .Value(P.mIndex)
			    .Value(P.mOffset)
			    .Value(P.mbDuplicate)
			    .Value(reinterpret_cast<uintptr_t>(P.mTables.get()))
			    .Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mPipelineStages.push_back({P.mShader});
			D.mPipelines.push_back({"default", 0, EArdaPipelineStateKind::Compute});
			FArdaDependencyShaderValue Value;
			Value.mMember = "mSelection";
			const FArdaBindlessPush Push{UINT32_MAX, 100u * (P.mIndex + 1)};
			Value.mBytes.resize(sizeof(Push));
			std::memcpy(Value.mBytes.data(), &Push, sizeof(Push));
			Value.mDescriptorIndices.push_back(
			    {"partitions", 0, P.mIndex, EArdaRHIBindingType::StructuredBufferUAV, 0});
			D.BindShader<FArdaBindlessShaderParameters>({}, "default", {Value});
			FArdaDependencyBindlessTable T;
			T.mName = "partitions";
			T.mCapacity = 2;
			T.mLayout.mMaxCapacity = 2;
			T.mLayout.mVisibility = EArdaRHIShaderStage::Compute;
			T.mLayout.mRegisterSpaces = {{0, 2, EArdaRHIBindingType::StructuredBufferUAV}};
			FArdaDependencyBindlessEntry E;
			E.mType = EArdaRHIBindingType::StructuredBufferUAV;
			E.mArrayElement = P.mIndex;
			E.mResource = P.mOutput;
			E.mAccess = EArdaDependencyAccess::Write;
			E.mView.mBufferRange = {P.mOffset, 32};
			T.mEntries.push_back(E);
			if (P.mbDuplicate)
			{
				T.mEntries.push_back(E);
			}
			D.mBindlessTables.push_back(eastl::move(T));
			return D;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			const auto& T = C.GetDescriptorTable("partitions");
			if (!T || T->GetCapacity() != 2 || T->GetDesc().mItems.size() != 1 || C.GetBindings().size() != 2)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "Automatic bindless table was not prepared.");
			}
			{
				static std::mutex Mutex;
				std::lock_guard<std::mutex> Lock(Mutex);
				P.mTables->push_back(T);
			}
			if (auto S = C.SetComputeState(); !S)
			{
				return S;
			}
			C.GetCommands().Dispatch(8, 1, 1);
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaBindlessTestNode, DispatchBindless);
	ARDA_DEFINE_GRAPH_NODE(FArdaBindlessTestNode, DispatchBindless);

	TEST_P(FArdaInductorGpu, AutomaticBindlessPartitionsAndPushParametersReplayWithoutRebinding)
	{
		FArdaDependencyGraph G(mDevice);
		FArdaGraphEdit Edit(G);
		FArdaInductorOptions O;
		O.mFramesInFlight = 1;
		O.mbEnableAsyncCompute = false;
		O.mbEnableCopyQueue = false;
		ASSERT_TRUE(Edit.SetOptions(O));
		FArdaBindlessTestParameters P;
		P.mShader = CreateConformanceShader("BindlessCS");
		ASSERT_TRUE(P.mShader);
		P.mTables = eastl::make_shared<eastl::vector<FArdaRHIDescriptorTableRef>>();
		const auto A = DispatchBindless(Edit, P, "lower");
		ASSERT_TRUE(A) << Edit.GetStatus().mMessage.c_str();
		P.mOutput = A.GetBuffer("Output");
		P.mIndex = 1;
		P.mOffset = 32;
		const auto B = DispatchBindless(Edit, P, "upper");
		ASSERT_TRUE(B) << Edit.GetStatus().mMessage.c_str();
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		const auto FirstRead = ReadbackNode(Edit, FArdaGraphReadbackParameters{P.mOutput, Bytes}, "read all");
		ASSERT_TRUE(FirstRead);
		const auto Before = G.GetPipelineCacheStats();
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = G.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		EXPECT_FALSE(G.GetTopology().IsReachable(A.GetHandle(), B.GetHandle()));
		EXPECT_FALSE(G.GetTopology().IsReachable(B.GetHandle(), A.GetHandle()));
		const auto CompiledCache = G.GetPipelineCacheStats();
		EXPECT_EQ(CompiledCache.mComputeEntries, Before.mComputeEntries);
		EXPECT_EQ(CompiledCache.mMisses, Before.mMisses);
		EXPECT_EQ(CompiledCache.mHits, Before.mHits);
		EXPECT_TRUE(P.mTables->empty());
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			SCOPED_TRACE(Frame);
			const auto E = G.Execute();
			ASSERT_TRUE(E.mStatus) << E.mStatus.mMessage.c_str();
			ASSERT_TRUE(mDevice->WaitForIdle());
			EXPECT_EQ(E.mStateConformanceFailureCount, 0u);
			const auto FrameCache = G.GetPipelineCacheStats();
			EXPECT_EQ(FrameCache.mComputeEntries, CompiledCache.mComputeEntries + 1);
			EXPECT_EQ(FrameCache.mMisses, CompiledCache.mMisses + 1);
			EXPECT_EQ(FrameCache.mHits, CompiledCache.mHits + 1);
			EXPECT_EQ(FrameCache.mCreateFailures, 0u);
			ASSERT_EQ(P.mTables->size(), (Frame + 1) * 2u);
			ASSERT_EQ(Bytes->size(), 64u);
			for (uint32_t I = 0; I < 16; ++I)
			{
				uint32_t V;
				std::memcpy(&V, Bytes->data() + I * 4, 4);
				EXPECT_EQ(V, (I < 8 ? 100u : 200u) + I % 8);
			}
		}
		ASSERT_EQ(P.mTables->size(), 6u);
		EXPECT_EQ(eastl::count(P.mTables->begin(), P.mTables->end(), (*P.mTables)[0]), 3);
		EXPECT_EQ(eastl::count(P.mTables->begin(), P.mTables->end(), (*P.mTables)[1]), 3);
		EXPECT_NE((*P.mTables)[0], (*P.mTables)[1]);
		const auto PreparedCache = G.GetPipelineCacheStats();
		// Reconstruct the next revision, then expose the same bytes through another descriptor index.
		FArdaGraphEdit AliasEdit(G);
		ASSERT_TRUE(AliasEdit.SetOptions(O));
		P.mOutput = {};
		P.mIndex = 0;
		P.mOffset = 0;
		const auto NextA = DispatchBindless(AliasEdit, P, "lower");
		ASSERT_TRUE(NextA);
		P.mOutput = NextA.GetBuffer("Output");
		P.mIndex = 1;
		P.mOffset = 32;
		ASSERT_TRUE(DispatchBindless(AliasEdit, P, "upper"));
		const auto NextRead = ReadbackNode(AliasEdit, FArdaGraphReadbackParameters{P.mOutput, Bytes}, "read all");
		ASSERT_TRUE(NextRead);
		P.mOffset = 0;
		const auto Alias = DispatchBindless(AliasEdit, P, "overlapping alias");
		ASSERT_TRUE(Alias);
		auto FinalBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		const auto FinalRead =
		    ReadbackNode(AliasEdit, FArdaGraphReadbackParameters{P.mOutput, FinalBytes}, "read overwritten contents");
		ASSERT_TRUE(FinalRead);
		ASSERT_TRUE(AliasEdit.Commit());
		const auto Recompiled = G.Compile();
		ASSERT_TRUE(Recompiled) << Recompiled.mMessage.c_str();
		const auto RecompiledCache = G.GetPipelineCacheStats();
		EXPECT_EQ(RecompiledCache.mComputeEntries, PreparedCache.mComputeEntries);
		EXPECT_EQ(RecompiledCache.mMisses, PreparedCache.mMisses);
		EXPECT_EQ(RecompiledCache.mHits, PreparedCache.mHits);
		EXPECT_EQ(P.mTables->size(), 6u);
		EXPECT_TRUE(G.GetTopology().FindEdge(NextA.GetHandle(), Alias.GetHandle()));
		EXPECT_TRUE(G.GetTopology().FindEdge(NextRead.GetHandle(), Alias.GetHandle()));
		EXPECT_TRUE(G.GetTopology().FindEdge(Alias.GetHandle(), FinalRead.GetHandle()));
		EXPECT_FALSE(G.GetTopology().FindEdge(NextA.GetHandle(), FinalRead.GetHandle()));
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			SCOPED_TRACE(Frame);
			Bytes->assign(64, 0xff);
			FinalBytes->assign(64, 0xff);
			const auto Executed = G.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(Executed.mStateConformanceFailureCount, 0u);
			const auto FrameCache = G.GetPipelineCacheStats();
			EXPECT_EQ(FrameCache.mComputeEntries, PreparedCache.mComputeEntries);
			EXPECT_EQ(FrameCache.mMisses, PreparedCache.mMisses);
			EXPECT_EQ(FrameCache.mHits, PreparedCache.mHits + 3);
			EXPECT_EQ(FrameCache.mCreateFailures, 0u);
			EXPECT_EQ(P.mTables->size(), 6u + (Frame + 1) * 3u);
			ASSERT_EQ(Bytes->size(), 64u);
			ASSERT_EQ(FinalBytes->size(), 64u);
			for (uint32_t Index = 0; Index < 16; ++Index)
			{
				uint32_t Before = 0, After = 0;
				std::memcpy(&Before, Bytes->data() + Index * sizeof(Before), sizeof(Before));
				std::memcpy(&After, FinalBytes->data() + Index * sizeof(After), sizeof(After));
				EXPECT_EQ(Before, (Index < 8 ? 100u : 200u) + Index % 8);
				EXPECT_EQ(After, 200u + Index % 8);
			}
		}
		FArdaGraphEdit InvalidEdit(G);
		P.mOutput = {};
		P.mbDuplicate = true;
		EXPECT_FALSE(DispatchBindless(InvalidEdit, P, "duplicate index"));
		ASSERT_TRUE(InvalidEdit.Cancel());
	}

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaLocalRayParameters)
		ARDA_SHADER_BUFFER_UAV(mOutput, 0, 4, EArdaRHIShaderStage::RayGeneration)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	struct FArdaTableTestParameters
	{
		FArdaRHIShaderRef mRaygen;
		eastl::vector<FArdaGraphRayShader> mShaders;
		uint32_t mCase = 0;
		eastl::shared_ptr<eastl::vector<FArdaRHIShaderTableRef>> mTrace;
		FArdaDependencyResourceHandle mOutput;
	};

	struct FArdaRayMissParameters
	{
		FArdaRHIShaderRef mShader;
	};

	struct FArdaRayMissNode : TArdaNodeDefinition<FArdaRayMissNode, FArdaRayMissParameters, EArdaNodeType::RayMiss>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.automatic-ray-miss", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder().Value(P.mShader->GetPersistentCacheHash()).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelineStages = {{P.mShader, {}, "miss"}};
			return Desc;
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaRayMissNode, RayTableMiss);
	ARDA_DEFINE_GRAPH_NODE(FArdaRayMissNode, RayTableMiss);

	struct FArdaRayTableTestNode
	    : TArdaNodeDefinition<FArdaRayTableTestNode, FArdaTableTestParameters, EArdaNodeType::RayGeneration>
	{
		using FArdaParameters = FArdaTableTestParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.automatic-ray-table", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaTableTestParameters& P)
		{
			return FArdaDependencyKeyBuilder{}
			    .Value(P.mRaygen->GetPersistentCacheHash())
			    .Value(P.mCase)
			    .Resource(P.mOutput)
			    .Value(reinterpret_cast<uintptr_t>(P.mTrace.get()))
			    .Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaTableTestParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc N;
			N.mbSideEffect = true;
			N.mPipelineStages = {{P.mRaygen, {}, "raygen"}};
			auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Config->mKind = EArdaPipelineStateKind::RayTracing;
			Config->mRayTracing.mDesc.mMaxPayloadSize = 4;
			N.mPipelines = {{"default", 0, EArdaPipelineStateKind::RayTracing, {}, Config}};

			// Vary shader-table records to exercise export, index and local-argument validation.
			if (P.mCase)
			{
				FArdaDependencyShaderTable T;
				T.mRecords = {{EArdaRHIShaderTableRecordType::RayGeneration, 0, "raygen"},
				    {EArdaRHIShaderTableRecordType::Miss, 0, "miss"}};
				if (P.mCase == 2)
				{
					T.mRecords[1].mExportName = "missing";
				}
				if (P.mCase == 3)
				{
					T.mRecords[1].mExportName = "raygen";
				}
				if (P.mCase == 4)
				{
					T.mRecords[1].mRecordIndex = 1;
				}
				if (P.mCase == 5)
				{
					T.mRecords.push_back(T.mRecords[1]);
				}
				if (P.mCase == 6)
				{
					auto& Local = T.mRecords[0].mLocalBindings;
					Local.mMetadata = &FArdaLocalRayParameters::GetStaticMetadata();
					Local.mResources = {{"mOutput", P.mOutput}};
				}
				N.mShaderTables.push_back(eastl::move(T));
			}

			return N;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaTableTestParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			if (!C.GetShaderTable() || C.GetShaderTable()->GetEntryCount() != 2)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Expected two prepared ray records.");
			}

			// Capture the persistent table before recording the ray dispatch.
			P.mTrace->push_back(C.GetShaderTable());
			if (auto S = C.SetRayTracingState(); !S)
			{
				return S;
			}
			return C.GetCommands().DispatchRays(1, 1, 1);
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaRayTableTestNode, TraceRayTable);
	ARDA_DEFINE_GRAPH_NODE(FArdaRayTableTestNode, TraceRayTable);

	TEST_P(FArdaInductorGpu, ShaderTablesPrepareReplayAndRejectInvalidExportRecords)
	{
		const auto Raygen =
		    CreateConformanceShader("EmptyRaygen", EArdaRHIShaderStage::RayGeneration, "ArdaGraphTables.hlsl");
		const auto Miss = CreateConformanceShader("EmptyMiss", EArdaRHIShaderStage::Miss, "ArdaGraphTables.hlsl");
		ASSERT_TRUE(Raygen && Miss);
		for (uint32_t Case = 0; Case < 7; ++Case)
		{
			SCOPED_TRACE(Case);
			FArdaDependencyGraph G(mDevice);
			FArdaGraphEdit Edit(G);
			const auto MissStage = RayTableMiss(Edit, {Miss});
			ASSERT_TRUE(MissStage);
			FArdaTableTestParameters P{Raygen,
			    {MissStage},
			    Case,
			    eastl::make_shared<eastl::vector<FArdaRHIShaderTableRef>>()};
			auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
			if (Case == 6)
			{
				P.mRaygen =
				    CreateConformanceShader("LocalRaygen", EArdaRHIShaderStage::RayGeneration, "ArdaGraphTables.hlsl");
				ASSERT_TRUE(P.mRaygen);
				FArdaRHIBufferDesc B;
				B.mByteSize = B.mStructureStride = 4;
				B.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
				const auto Output = AllocateBuffer(Edit, B, "local-output");
				ASSERT_TRUE(Output);
				P.mOutput = Output.GetHandle();
				ASSERT_TRUE(ReadbackNode(Edit, {P.mOutput, Bytes}, "read-local"));
			}
			const auto N = TraceRayTable(Edit, P, "trace");
			ASSERT_TRUE(N);
			const auto Before = G.GetPipelineCacheStats();
			ASSERT_TRUE(Edit.Commit());
			const auto Compiled = G.Compile();
			const auto CompiledCache = G.GetPipelineCacheStats();
			EXPECT_EQ(CompiledCache.mRayTracingEntries, Before.mRayTracingEntries);
			EXPECT_EQ(CompiledCache.mMisses, Before.mMisses);
			EXPECT_EQ(CompiledCache.mHits, Before.mHits);
			EXPECT_EQ(CompiledCache.mCreateFailures, 0u);
			EXPECT_TRUE(P.mTrace->empty());
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			if (Case == 6 && std::string(GetParam()) == "native-vulkan")
			{
				// Local descriptor support is checked when resolving the native pipeline.
				const auto Submitted = G.Submit();
				ASSERT_FALSE(Submitted);
				EXPECT_EQ(Submitted.mStatus.mCode, EArdaRHIResult::Unsupported);
				EXPECT_FALSE(Submitted.mValue);
				EXPECT_TRUE(P.mTrace->empty());
				continue;
			}
			if (Case >= 2 && Case <= 5)
			{
				// Export and record validation runs when the table is realized. Failed preparation is retryable.
				const auto Revision = G.GetCompileResult().mRevision;
				for (uint32_t Attempt = 0; Attempt < 2; ++Attempt)
				{
					SCOPED_TRACE(Attempt);
					const auto Submitted = G.Submit();
					ASSERT_FALSE(Submitted);
					EXPECT_FALSE(Submitted.mValue);
					EXPECT_EQ(Submitted.mStatus.mCode, EArdaRHIResult::InvalidArgument);
					EXPECT_TRUE(P.mTrace->empty());
					EXPECT_EQ(G.GetCompileResult().mRevision, Revision);
					const auto FailedCache = G.GetPipelineCacheStats();
					EXPECT_EQ(FailedCache.mRayTracingEntries, CompiledCache.mRayTracingEntries + 1);
					EXPECT_EQ(FailedCache.mMisses, CompiledCache.mMisses + 1);
					EXPECT_EQ(FailedCache.mHits, CompiledCache.mHits + Attempt);
					EXPECT_EQ(FailedCache.mCreateFailures, 0u);
				}
				continue;
			}
			for (uint32_t Frame = 0; Frame < 3; ++Frame)
			{
				SCOPED_TRACE(Frame);
				const auto E = G.Execute();
				ASSERT_TRUE(E.mStatus) << E.mStatus.mMessage.c_str();
				ASSERT_TRUE(mDevice->WaitForIdle());
				ASSERT_EQ(P.mTrace->size(), Frame + 1);
				EXPECT_EQ(P.mTrace->back(), P.mTrace->front());
				const auto FrameCache = G.GetPipelineCacheStats();
				EXPECT_EQ(FrameCache.mRayTracingEntries, CompiledCache.mRayTracingEntries + 1);
				EXPECT_EQ(FrameCache.mMisses, CompiledCache.mMisses + 1);
				EXPECT_EQ(FrameCache.mHits, CompiledCache.mHits);
				EXPECT_EQ(FrameCache.mCreateFailures, 0u);
				if (Case == 6)
				{
					ASSERT_EQ(Bytes->size(), sizeof(uint32_t));
					uint32_t Value = 0;
					std::memcpy(&Value, Bytes->data(), sizeof(Value));
					EXPECT_EQ(Value, 123u);
				}
			}
		}
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaInductorGpu, testing::Values("native-d3d12", "native-vulkan"));
}
