#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaDependencyGraph.h"
#include "ArdaDependencyGraphNodes.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"
#include "ShaderStructs/ArdaShaderCompiler.h"

#include <gtest/gtest.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace
{
	using namespace arda;

	class FInductorGpuDiagnostics final : public IArdaDiagnosticCallback
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

	struct FInductorPipelineTrace
	{
		eastl::vector<FArdaRHIComputePipelineRef> mPipelines;
		eastl::vector<uint64_t> mKeys;
	};

	struct FInductorAutoComputeParameters
	{
		FArdaDependencyResourceHandle mFirst;
		FArdaDependencyResourceHandle mSecond;
		FArdaRHIShaderRef mShader;
		eastl::vector<FArdaRHIBindingLayoutRef> mLayouts;
		uint32_t mCount = 16;
		bool mbStageOnly = false;
		eastl::shared_ptr<FInductorPipelineTrace> mTrace;
	};

	FArdaRHIStatus RegisterAutoComputeTestNode()
	{
		static const FArdaRHIStatus Status = []
		{
			TArdaDependencyNodeDefinition<FInductorAutoComputeParameters> Definition;
			Definition.mName = "test.inductor.auto-compute-pipeline";
			Definition.mKind = EArdaDependencyNodeKind::Compute;
			Definition.mCanonicalKey = [](const FInductorAutoComputeParameters& P)
			{
				eastl::string Key;
				auto Add = [&](uint64_t Value)
				{
					for (uint32_t Shift = 0; Shift < 64; Shift += 8)
					{
						Key.push_back(static_cast<char>(Value >> Shift));
					}
				};
				Add(P.mFirst.mGraph);
				Add(P.mFirst.mIndex);
				Add(P.mFirst.mGeneration);
				Add(P.mSecond.mGraph);
				Add(P.mSecond.mIndex);
				Add(P.mSecond.mGeneration);
				Add(P.mShader ? P.mShader->GetPersistentCacheHash() : 0);
				Add(P.mCount);
				Add(P.mbStageOnly);
				Add(reinterpret_cast<uintptr_t>(P.mTrace.get()));
				Add(P.mLayouts.size());
				for (const auto& Layout : P.mLayouts)
				{
					Add(Layout ? HashValue(Layout->GetDesc()) : 0);
				}
				return Key;
			};
			Definition.mDescribe = [](const FInductorAutoComputeParameters& P)
			{
				FArdaDependencyNodeDesc Desc;
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
				Desc.mAccesses = {{P.mFirst, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess},
				    {P.mSecond, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}};
				FArdaInductorPipelineRequest Request;
				Request.mSlot = "default";
				Request.mKind = EArdaPipelineStateKind::Compute;
				Desc.mPipelines.push_back(eastl::move(Request));
				return Desc;
			};
			Definition.mRecord = [](FArdaDependencyExecutionContext& Context, const FInductorAutoComputeParameters& P)
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
				P.mTrace->mPipelines.push_back(Pipeline->mCompute);
				P.mTrace->mKeys.push_back(Pipeline->mStableKey);
				FArdaRHIComputeState State;
				State.mPipeline = Pipeline->mCompute;
				for (const auto& Layout : Pipeline->mCompute->GetDesc().mBindingLayouts)
				{
					FArdaRHIBindingSetDesc Bindings;
					Bindings.mLayout = Layout;
					FArdaRHIBindingItem Item;
					Item.mSlot = Layout->GetDesc().mItems.front().mSlot;
					Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
					Item.mResource = Context.GetBuffer(Layout->GetDesc().mRegisterSpace == 3 ? P.mFirst : P.mSecond);
					Item.mView.mBufferRange = {0, uint64_t(P.mCount) * 4};
					Bindings.mItems.push_back(eastl::move(Item));
					auto Created = Context.GetDevice()->CreateBindingSet(Bindings);
					if (!Created)
					{
						return Created.mStatus;
					}
					State.mBindings.push_back(eastl::move(Created.mValue));
				}
				if (auto Bound = Context.GetCommands().SetComputeState(State); !Bound)
				{
					return Bound;
				}
				Context.GetCommands().Dispatch(P.mCount, 1, 1);
				return FArdaRHIStatus{};
			};
			return FArdaNodeRegistry::Get().Register(eastl::move(Definition));
		}();
		return Status;
	}

	class ArdaInductorGpu : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			if (!FindBackendModule(GetParam()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			FArdaBackendConfiguration Configuration;
			Configuration.mBackendName = GetParam();
			Configuration.mbEnableValidation = true;
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

		FArdaRHIShaderRef CreateConformanceShader()
		{
			const auto Path =
			    (std::filesystem::path(ARDA_RDG_TEST_SHADER_SOURCE_DIR) / "ArdaGraphConformance.hlsl").string();
			FArdaShaderTypeRegistration Registration("InductorRegisteredCS",
			    Path.c_str(),
			    "RegisteredCS",
			    "RegisteredCS",
			    EArdaRHIShaderStage::Compute,
			    nullptr);
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
			Desc.mStage = EArdaRHIShaderStage::Compute;
			Desc.mEntryPoint = "RegisteredCS";
			Desc.mBytecode = Bytecode.data();
			Desc.mBytecodeSize = Bytecode.size();
			auto Shader = mDevice->CreateShader(Desc);
			EXPECT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
			return Shader.mValue;
		}

		FInductorGpuDiagnostics mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(ArdaInductorGpu, CreatesComputePipelineBeforeExecutionAndReusesItAcrossFrames)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		ASSERT_TRUE(RegisterAutoComputeTestNode());
		uint32_t DirectSubmittedLists = 0;
		for (bool bStageAncestry : {false, true})
		{
			SCOPED_TRACE(bStageAncestry ? "stage -> sync -> consumer" : "direct stage contribution");
			FInductorAutoComputeParameters Parameters;
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
			Parameters.mTrace = eastl::make_shared<FInductorPipelineTrace>();
			auto FirstBytes = eastl::make_shared<eastl::vector<uint8_t>>();
			auto SecondBytes = eastl::make_shared<eastl::vector<uint8_t>>();
			FArdaDependencyGraph Graph(mDevice);
			ASSERT_TRUE(Graph.BeginGraphEdit());
			FArdaInductorOptions Options;
			Options.mbEnableAsyncCompute = false;
			Options.mbEnableCopyQueue = false;
			ASSERT_TRUE(Graph.SetOptions(Options));
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = Parameters.mCount * 4;
			Desc.mStructureStride = 4;
			Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
			const auto First = Graph.CreateBuffer("First compute output", Desc);
			const auto Second = Graph.CreateBuffer("Second compute output", Desc);
			ASSERT_TRUE(First);
			ASSERT_TRUE(Second);
			Parameters.mFirst = First.mValue;
			Parameters.mSecond = Second.mValue;
			// Consumers are attached before the producer; resource semantics must determine order.
			const auto FirstRead = Graph.AttachOrFind("Read first",
			    "arda.readback",
			    FArdaGraphReadbackParameters{First.mValue, FirstBytes});
			const auto SecondRead = Graph.AttachOrFind("Read second",
			    "arda.readback",
			    FArdaGraphReadbackParameters{Second.mValue, SecondBytes});
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
			const auto Produce =
			    Graph.AttachOrFind("Run inferred compute", "test.inductor.auto-compute-pipeline", Parameters);
			ASSERT_TRUE(Produce);
			FArdaGraphNodeHandle Stage, Sync;
			if (bStageAncestry)
			{
				const auto SyncResult =
				    Graph.AttachOrFind("Stage ancestry join", "arda.sync", FArdaGraphSyncParameters{});
				const auto StageResult = Graph.AttachOrFind("Compute stage declaration",
				    "test.inductor.auto-compute-pipeline",
				    StageParameters);
				ASSERT_TRUE(SyncResult);
				ASSERT_TRUE(StageResult);
				Stage = StageResult.mValue;
				Sync = SyncResult.mValue;
				ASSERT_TRUE(Graph.AddDependency(Stage, Sync));
				ASSERT_TRUE(Graph.AddDependency(Sync, Produce.mValue));
			}
			const auto Before = Graph.GetPipelineCacheStats();
			const auto Compiled = Graph.EndGraphEdit();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			ASSERT_TRUE(Graph.GetCompileResult().mStatus);
			uint64_t Revision = Graph.GetCompileResult().mRevision;
			EXPECT_GT(Revision, 0u);
			auto CompiledCache = Graph.GetPipelineCacheStats();
			EXPECT_EQ(CompiledCache.mComputeEntries, Before.mComputeEntries + 1);
			EXPECT_EQ(CompiledCache.mMisses, Before.mMisses + 1);
			EXPECT_EQ(CompiledCache.mCreateFailures, 0u);
			EXPECT_TRUE(Parameters.mTrace->mPipelines.empty()); // PSO creation is compile work, not a record callback.
			const auto& Order = Graph.GetCompileResult().mExecutionOrder;
			ASSERT_EQ(Order.size(), bStageAncestry ? 5u : 3u);
			if (bStageAncestry)
			{
				EXPECT_EQ(Order[0], Stage);
				EXPECT_EQ(Order[1], Sync);
				EXPECT_EQ(Order[2], Produce.mValue);
			}
			else
			{
				EXPECT_EQ(Order.front(), Produce.mValue);
			}
			for (uint32_t Frame = 0; Frame < 4; ++Frame)
			{
				SCOPED_TRACE(Frame);
				if (Frame == 3)
				{
					ASSERT_TRUE(Graph.BeginGraphEdit());
					const auto Recompiled = Graph.EndGraphEdit();
					ASSERT_TRUE(Recompiled) << Recompiled.mMessage.c_str();
					EXPECT_GT(Graph.GetCompileResult().mRevision, Revision);
					Revision = Graph.GetCompileResult().mRevision;
					const auto RecompiledCache = Graph.GetPipelineCacheStats();
					EXPECT_EQ(RecompiledCache.mComputeEntries, CompiledCache.mComputeEntries);
					EXPECT_EQ(RecompiledCache.mMisses, CompiledCache.mMisses);
					EXPECT_EQ(RecompiledCache.mHits, CompiledCache.mHits + 1);
					EXPECT_EQ(RecompiledCache.mCreateFailures, 0u);
					CompiledCache = RecompiledCache;
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
				const auto FrameCache = Graph.GetPipelineCacheStats();
				EXPECT_EQ(FrameCache.mComputeEntries, CompiledCache.mComputeEntries);
				EXPECT_EQ(FrameCache.mMisses, CompiledCache.mMisses);
				EXPECT_EQ(FrameCache.mHits, CompiledCache.mHits);
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

	struct FInductorFrameTrace
	{
		eastl::vector<uint32_t> mSlots;
		eastl::vector<const void*> mBuffers;
		eastl::vector<eastl::shared_ptr<eastl::vector<uint8_t>>> mOutputs;
	};

	struct FInductorFrameParameters
	{
		FArdaDependencyResourceHandle mResource;
		eastl::shared_ptr<FInductorFrameTrace> mTrace;
	};

	TEST_P(ArdaInductorGpu, ThreeFrameSlotsPreserveReadbacksAndTicketsAcrossRecyclingAndEditing)
	{
		TArdaDependencyNodeDefinition<FInductorFrameParameters> Write;
		Write.mName = "test.inductor.frame-write";
		Write.mKind = EArdaDependencyNodeKind::Compute;
		Write.mCanonicalKey = [](const auto& P)
		{
			return eastl::string(std::to_string(P.mResource.mIndex).c_str());
		};
		Write.mDescribe = [](const auto& P)
		{
			FArdaDependencyNodeDesc Desc;
			FArdaDependencyAccess Access;
			Access.mResource = P.mResource;
			Access.mAccess = EArdaDependencyAccess::Write;
			Access.mState = EArdaRHIResourceState::UnorderedAccess;
			Desc.mAccesses.push_back(Access);
			return Desc;
		};
		Write.mRecord = [](FArdaDependencyExecutionContext& C, const auto& P)
		{
			const auto Buffer = C.GetBuffer(P.mResource);
			P.mTrace->mSlots.push_back(C.GetFrameIndex());
			P.mTrace->mBuffers.push_back(Buffer.Get());
			return C.GetCommands().ClearBufferUInt(*Buffer, static_cast<uint32_t>(C.GetFrameSequence()));
		};
		TArdaDependencyNodeDefinition<FInductorFrameParameters> Read;
		Read.mName = "test.inductor.frame-read";
		Read.mKind = EArdaDependencyNodeKind::Copy;
		Read.mCanonicalKey = Write.mCanonicalKey;
		Read.mDescribe = [](const auto& P)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			FArdaDependencyAccess Access;
			Access.mResource = P.mResource;
			Access.mAccess = EArdaDependencyAccess::Read;
			Access.mState = EArdaRHIResourceState::CopySource;
			Desc.mAccesses.push_back(Access);
			return Desc;
		};
		Read.mRecord = [](FArdaDependencyExecutionContext& C, const auto& P)
		{
			return C.ReadbackBuffer(P.mResource, P.mTrace->mOutputs[C.GetFrameSequence() - 1]);
		};
		ASSERT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(Write)));
		ASSERT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(Read)));

		struct FUnregister
		{
			~FUnregister()
			{
				(void)FArdaNodeRegistry::Get().Unregister("test.inductor.frame-read");
				(void)FArdaNodeRegistry::Get().Unregister("test.inductor.frame-write");
			}
		} Unregister;

		for (bool Persistent : {false, true})
		{
			SCOPED_TRACE(Persistent);
			FArdaDependencyGraph Graph(mDevice);
			ASSERT_TRUE(Graph.BeginGraphEdit());
			FArdaInductorOptions Options;
			Options.mFramesInFlight = 3;
			Options.mbEnableGpuTiming = true;
			ASSERT_TRUE(Graph.SetOptions(Options));
			FArdaDependencyResourceDesc Desc;
			Desc.mName = "frame storage";
			Desc.mbPersistent = Persistent;
			Desc.mBuffer.mByteSize = 64;
			Desc.mBuffer.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto Resource = Graph.CreateResource(Desc);
			ASSERT_TRUE(Resource);
			auto Trace = eastl::make_shared<FInductorFrameTrace>();
			for (uint32_t I = 0; I < 4; ++I)
			{
				Trace->mOutputs.push_back(eastl::make_shared<eastl::vector<uint8_t>>());
			}
			const FInductorFrameParameters Parameters{Resource.mValue, Trace};
			ASSERT_TRUE(Graph.AttachOrFind("frame writer", "test.inductor.frame-write", Parameters));
			ASSERT_TRUE(Graph.AttachOrFind("frame reader", "test.inductor.frame-read", Parameters));
			const auto Compiled = Graph.EndGraphEdit();
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
			ASSERT_TRUE(Graph.BeginGraphEdit());
			ASSERT_TRUE(Graph.EndGraphEdit());
			EXPECT_TRUE(Graph.GetTimingProfile().empty());
			EXPECT_TRUE(Graph.Wait(Tickets.front()).mStatus);
			EXPECT_TRUE(Graph.IsComplete(Tickets.front()).mValue);
		}
	}

	struct FInductorImageAliasParameters
	{
		FArdaDependencyResourceHandle mTexture, mBuffer;
		uint32_t mValue = 0;
	};

	TEST_P(ArdaInductorGpu, OverlappingPlacedImagesReactivateAcrossQueuesAndRepeatedFrames)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		TArdaDependencyNodeDefinition<FInductorImageAliasParameters> Clear;
		Clear.mName = "test.inductor.alias-image-clear";
		Clear.mKind = EArdaDependencyNodeKind::Compute;
		Clear.mCanonicalKey = [](const auto& P)
		{
			return eastl::string(std::to_string(P.mTexture.mIndex).c_str());
		};
		Clear.mDescribe = [](const auto& P)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mAccesses.push_back(
			    {P.mTexture, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
			return Desc;
		};
		Clear.mRecord = [](FArdaDependencyExecutionContext& C, const auto& P)
		{
			return C.GetCommands().ClearTextureUInt(*C.GetTexture(P.mTexture), {}, P.mValue);
		};
		TArdaDependencyNodeDefinition<FInductorImageAliasParameters> Copy;
		Copy.mName = "test.inductor.alias-image-copy";
		Copy.mKind = EArdaDependencyNodeKind::Copy;
		Copy.mCanonicalKey = Clear.mCanonicalKey;
		Copy.mDescribe = [](const auto& P)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mAccesses.push_back({P.mTexture, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource});
			Desc.mAccesses.push_back({P.mBuffer, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest});
			return Desc;
		};
		Copy.mRecord = [](FArdaDependencyExecutionContext& C, const auto& P)
		{
			return C.GetCommands().CopyTextureToBuffer(*C.GetBuffer(P.mBuffer),
			    {0, 1024},
			    *C.GetTexture(P.mTexture),
			    {});
		};
		ASSERT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(Clear)));
		ASSERT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(Copy)));

		struct FUnregister
		{
			~FUnregister()
			{
				(void)FArdaNodeRegistry::Get().Unregister("test.inductor.alias-image-copy");
				(void)FArdaNodeRegistry::Get().Unregister("test.inductor.alias-image-clear");
			}
		} Unregister;

		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Memory;
		Options.mFramesInFlight = 2;
		ASSERT_TRUE(Graph.SetOptions(Options));
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = TextureDesc.mHeight = 256;
		TextureDesc.mFormat = EArdaRHIFormat::R32UInt;
		TextureDesc.mUsage = EArdaRHITextureUsage::UnorderedAccess;
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 256 * 1024;
		eastl::array<eastl::shared_ptr<eastl::vector<uint8_t>>, 2> Outputs;
		FArdaGraphNodeHandle PreviousCopy;
		for (uint32_t I = 0; I < 2; ++I)
		{
			const eastl::string Suffix = std::to_string(I).c_str();
			const auto Texture = Graph.CreateTexture("image " + Suffix, TextureDesc);
			const auto Buffer = Graph.CreateBuffer("capture " + Suffix, BufferDesc);
			ASSERT_TRUE(Texture);
			ASSERT_TRUE(Buffer);
			const FInductorImageAliasParameters P{Texture.mValue, Buffer.mValue, 100 + I};
			const auto Writer = Graph.AttachOrFind("clear image " + Suffix, "test.inductor.alias-image-clear", P);
			const auto Copied = Graph.AttachOrFind("capture image " + Suffix, "test.inductor.alias-image-copy", P);
			ASSERT_TRUE(Writer);
			ASSERT_TRUE(Copied);
			if (PreviousCopy)
			{
				ASSERT_TRUE(Graph.AddDependency(PreviousCopy, Writer.mValue));
			}
			PreviousCopy = Copied.mValue;
			Outputs[I] = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(Graph.AttachOrFind("read image " + Suffix,
			    "arda.readback",
			    FArdaGraphReadbackParameters{Buffer.mValue, Outputs[I]}));
		}
		const auto Compiled = Graph.EndGraphEdit();
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

	struct FInductorFailureParameters
	{
		FArdaDependencyResourceHandle mOutput, mUndeclared;
		bool mbFail = false;
	};

	TEST_P(ArdaInductorGpu, RecordingFailureAndUndeclaredAccessPreventSubmissionAndPermitRepair)
	{
		const eastl::string DefinitionName = "test.inductor.failure";
		TArdaDependencyNodeDefinition<FInductorFailureParameters> Definition;
		Definition.mName = DefinitionName;
		Definition.mKind = EArdaDependencyNodeKind::Compute;
		Definition.mCanonicalKey = [](const auto& P)
		{
			eastl::string K;
			for (uint64_t V : {P.mOutput.mGraph,
			         uint64_t(P.mOutput.mIndex),
			         P.mOutput.mGeneration,
			         P.mUndeclared.mGraph,
			         uint64_t(P.mUndeclared.mIndex),
			         P.mUndeclared.mGeneration,
			         uint64_t(P.mbFail)})
			{
				K.append(reinterpret_cast<const char*>(&V), sizeof(V));
			}
			return K;
		};
		Definition.mDescribe = [](const auto& P)
		{
			FArdaDependencyNodeDesc D;
			D.mAccesses.push_back({P.mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
			return D;
		};
		Definition.mRecord = [](FArdaDependencyExecutionContext& C, const auto& P)
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
		};
		ASSERT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(Definition)));

		struct FUnregister
		{
			~FUnregister()
			{
				(void)FArdaNodeRegistry::Get().Unregister("test.inductor.failure");
			}
		} Unregister;

		for (bool Undeclared : {false, true})
		{
			SCOPED_TRACE(Undeclared);
			FArdaDependencyGraph G(mDevice);
			ASSERT_TRUE(G.BeginGraphEdit());
			FArdaRHIBufferDesc D;
			D.mByteSize = 64;
			D.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto Output = G.CreateBuffer("output", D).mValue;
			const auto Hidden = G.CreateBuffer("hidden", D).mValue;
			auto Writer = G.AttachOrFind("writer",
			    DefinitionName,
			    FInductorFailureParameters{Output, Undeclared ? Hidden : FArdaDependencyResourceHandle{}, !Undeclared});
			ASSERT_TRUE(Writer);
			auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(G.AttachOrFind("read", "arda.readback", FArdaGraphReadbackParameters{Output, Bytes}));
			ASSERT_TRUE(G.EndGraphEdit());
			const auto Failed = G.Execute();
			EXPECT_FALSE(Failed.mStatus);
			EXPECT_EQ(Failed.mSubmittedCommandListCount, 0u);
			EXPECT_TRUE(Bytes->empty());
			ASSERT_TRUE(G.BeginGraphEdit());
			ASSERT_TRUE(G.RemoveNode(Writer.mValue));
			ASSERT_TRUE(G.AttachOrFind("writer", DefinitionName, FInductorFailureParameters{Output, {}, false}));
			ASSERT_TRUE(G.AttachOrFind("read", "arda.readback", FArdaGraphReadbackParameters{Output, Bytes}));
			ASSERT_TRUE(G.EndGraphEdit());
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

	TEST_P(ArdaInductorGpu, PreservesCurrentImportedTextureStateAndIgnoresUnusedUninitializedImports)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = TextureDesc.mHeight = 4;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.ImportTexture("unused uninitialized", Unused.mValue));
		auto Texture = Graph.ImportTexture("current state differs from creation", Used.mValue);
		ASSERT_TRUE(Texture);
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 1024;
		auto Buffer = Graph.CreateBuffer("pitched upload", BufferDesc);
		ASSERT_TRUE(Buffer);
		FArdaGraphUploadParameters Upload;
		Upload.mDestination = Buffer.mValue;
		Upload.mBytes.resize(1024, 0x7F);
		ASSERT_TRUE(Graph.AttachOrFind("upload", "arda.upload", Upload));
		ASSERT_TRUE(AttachArdaBufferToTexture(Graph, "copy", Texture.mValue, {}, Buffer.mValue, {0, 256}));
		ASSERT_TRUE(Graph.MarkOutput(Texture.mValue));
		const auto Compiled = Graph.EndGraphEdit();
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

	INSTANTIATE_TEST_SUITE_P(Native, ArdaInductorGpu, testing::Values("native-d3d12", "native-vulkan"));
}
