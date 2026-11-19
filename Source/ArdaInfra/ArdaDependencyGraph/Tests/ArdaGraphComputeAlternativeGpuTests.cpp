#include "ArdaBackend.h"
#include "ArdaNativeCompute.h"
#include "ArdaTestBackend.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaNodeDefinition.h"
#include <gtest/gtest.h>
#include <cstring>

namespace arda
{
	namespace
	{
		class FArdaGraphAlternativeShader final : public FArdaGlobalShader
		{
		public:
			ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
				ARDA_SHADER_BUFFER_UAV_ACCESS(mOutput,
				    0,
				    0,
				    EArdaRHIShaderStage::Compute,
				    EArdaShaderResourceAccess::Write)
			ARDA_END_SHADER_PARAMETER_STRUCT()
			ARDA_DECLARE_GLOBAL_SHADER(FArdaGraphAlternativeShader);
		};
		ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaGraphAlternativeShader,
		    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphComputeAlternative.hlsl",
		    "GraphComputeAlternative",
		    "AlternativeCS",
		    EArdaRHIShaderStage::Compute)

#define ARDA_GRAPH_ALTERNATIVE_FIELDS(VALUE, BUFFER, SURFACE) BUFFER(uint32_t, mOutput, EArdaComputeAccess::Write)
		ARDA_CUDA_PARAMETER_STRUCT(FArdaGraphAlternativeCudaParameters, ARDA_GRAPH_ALTERNATIVE_FIELDS)
#undef ARDA_GRAPH_ALTERNATIVE_FIELDS

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGraphAlternativeNodeParameters)
			ARDA_NODE_CUDA_BUFFER(mOutput, FArdaGraphAlternativeCudaParameters, mOutput)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mCount, 257)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaGraphStateTestSource final : public IArdaNativeKernelSource
		{
		public:
			const char* GetProviderId() const noexcept override { return "test.state-check"; }
			const char* GetFormat() const noexcept override { return "test"; }
		};

		class FArdaGraphStateTestProvider final : public IArdaNativeComputeProvider
		{
		public:
			mutable uint32_t mHandoffs = 0;

			const FArdaNativeProviderDesc& GetDesc() const noexcept override
			{
				static const FArdaNativeProviderDesc Description{
				    "test.state-check", {"native-d3d12", "native-vulkan"}, {"test"}};
				return Description;
			}

			FArdaRHIStatus CheckSupport(const FArdaNativeComputeContext&,
			    const FArdaNativeOperandDesc&,
			    const IArdaNativeKernelSource&) const override
			{
				return {};
			}

			TArdaRHIResult<FArdaNativeResourceHandoff> QueryHandoff(const FArdaNativeComputeContext&,
			    const FArdaNativeResourceSlot&,
			    const FArdaNativeResourceView&) const override
			{
				++mHandoffs;
				return {{EArdaNativeHandoff::Direct, EArdaRHIResourceState::CopyDest}, {}};
			}

			TArdaRHIResult<FArdaNativeWorkRef> Prepare(const FArdaNativeComputeContext&,
			    const FArdaNativeOperandDesc&,
			    const IArdaNativeKernelSource&,
			    const FArdaNativeInvocation&,
			    const FArdaNativeLaunch&) const override
			{
				ADD_FAILURE() << "The graph must reject the native route before preparation.";
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Unexpected native preparation.")};
			}
		};

		class FArdaGraphAlternativeNode final : public TArdaNodeDefinition<FArdaGraphAlternativeNode,
		                                            FArdaGraphAlternativeNodeParameters,
		                                            EArdaNodeType::Cuda>
		{
		public:
			struct FArdaState
			{
				FArdaGlobalShaderMap mShaders;
				FArdaInductorPipelineContribution mStage;
			};

			inline static uint32_t mComputeRecordCount = 0;
			inline static uint32_t mCudaRecordCount = 0;

			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.graph.native-compute-alternative", 1};
			}

			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
			{
				FArdaRHIBufferDesc Description;
				Description.mByteSize = uint64_t(Parameters.mCount) * sizeof(uint32_t);
				Description.mStructureStride = sizeof(uint32_t);
				Description.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
				Description.mbCudaInterop = !UsesComputeAlternative(Context.GetDevice());
				return Context.Buffer(Parameters.mOutput, "Output", Description);
			}

			static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
			{
				if (!UsesComputeAlternative(Device.Get()))
				{
					return {{},
					    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
					        "This test exercises a device without CUDA.")};
				}
				auto State = eastl::make_shared<FArdaState>();
				if (!State->mShaders.Initialize(Device))
				{
					const auto Diagnostics = State->mShaders.GetDiagnostics();
					return {{},
					    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
					        Diagnostics.empty() ? "Alternative shader initialization failed."
					                            : Diagnostics.back().mMessage.c_str())};
				}
				const auto* Shader = State->mShaders.Find(FArdaGraphAlternativeShader::GetStaticType());
				if (!Shader)
				{
					return {{},
					    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Alternative shader is missing.")};
				}
				State->mStage.mShader = Shader->GetShader();
				State->mStage.mBindingLayouts = Shader->GetBindingLayouts();
				return {eastl::move(State), {}};
			}

			static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State)
			{
				FArdaDependencyNodeDesc Description;
				Description.mPipelineStages = {State.mStage};
				FArdaInductorPipelineRequest Pipeline;
				Pipeline.mKind = EArdaPipelineStateKind::Compute;
				Description.mPipelines = {Pipeline};
				Description.BindShader<FArdaGraphAlternativeShader::FArdaParameters>(
				    {{"mOutput", Parameters.mOutput, 0, EArdaDependencyAccess::Write}});
				return Description;
			}

			static FArdaRHIStatus RecordComputeAlternative(FArdaDependencyExecutionContext& Context,
			    const FArdaParameters& Parameters,
			    const FArdaState&,
			    FArdaInstanceState&)
			{
				++mComputeRecordCount;
				FArdaNativeComputeRegistry Registry;
				auto Provider = eastl::make_shared<FArdaGraphStateTestProvider>();
				if (auto Status = Registry.RegisterProvider(Provider); !Status)
				{
					return Status;
				}
				FArdaNativeOperandDesc Operand;
				Operand.mId = "test.graph.state-qualified-operand";
				Operand.mResources = {{"output", EArdaRHIResourceType::Buffer, EArdaComputeAccess::Write}};
				Operand.mVariants.push_back({"test", eastl::make_shared<FArdaGraphStateTestSource>()});
				const auto* Pipeline = Context.GetPipeline();
				if (!Pipeline || !Pipeline->mCompute)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Missing test shader pipeline.");
				}
				FArdaRHIComputeState State{Pipeline->mCompute, Context.GetBindings()};
				Operand.mFallback.mPrepare =
				    [State = eastl::move(State), Groups = (Parameters.mCount + 63) / 64](const FArdaNativeComputeContext&,
				        const FArdaNativeInvocation&) -> TArdaRHIResult<FArdaNativeWorkRef>
				{
					return {MakeArdaNativeWork([State, Groups](IArdaRHICommandList& Commands)
					            {
						            if (auto Status = Commands.SetComputeState(State); !Status)
						            {
							            return Status;
						            }
						            Commands.Dispatch(Groups, 1, 1);
						            return FArdaRHIStatus{};
					            }),
					    {}};
				};
				if (auto Status = Registry.RegisterOperand(eastl::move(Operand)); !Status)
				{
					return Status;
				}
				FArdaNativeInvocation Invocation;
				Invocation.mResources = {{Context.GetBuffer(Parameters.mOutput.GetHandle())}};
				auto Status = Context.RecordNativeCompute(Registry,
				    "test.graph.state-qualified-operand",
				    Invocation,
				    EArdaNativeComputePreference::PreferNative);
				EXPECT_EQ(Provider->mHandoffs, 1u);
				return Status;
			}

			static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
			    const FArdaParameters&,
			    const FArdaState&,
			    FArdaInstanceState&,
			    FArdaCudaSequence&)
			{
				++mCudaRecordCount;
				return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
				    "A device without CUDA must never enter PrepareCuda.");
			}
		};

		class FArdaGraphComputeAlternativeGpu : public testing::TestWithParam<const char*>
		{
		protected:
			FArdaTestDiagnosticCallback mDiagnostics;
			FArdaShaderCompilerConfiguration mPreviousCompiler;

			void SetUp() override
			{
				ShutdownBackend();
				mPreviousCompiler = GetShaderCompilerConfiguration();
				auto Compiler = mPreviousCompiler;
				Compiler.mbCompileMissingArtifacts = true;
				Compiler.mbCompileOutdatedArtifacts = true;
				ConfigureShaderCompiler(Compiler);
				auto Configuration = MakeArdaTestBackendConfiguration();
				Configuration.mBackendName = GetParam();
				Configuration.mMessageCallback = &mDiagnostics;
				Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::OnDemand;
				Configuration.mShaderCacheDirectory = ARDA_BACKEND_TEST_SHADER_DIR;
				ASSERT_TRUE(ConfigureBackend(Configuration));
				ARDA_REQUIRE_BACKEND();
				if (GetDevice()->GetCudaCapabilities())
				{
					GTEST_SKIP() << "Run with ARDASHIR_ENABLE_CUDA=OFF to exercise the native graph alternative.";
				}
			}

			void TearDown() override
			{
				if (const auto Device = GetDevice())
				{
					EXPECT_TRUE(Device->WaitForIdle());
				}
				(void)FArdaNodeRegistry::Get().Unregister(FArdaGraphAlternativeNode::GetMetadata().mName);
				ShutdownBackend();
				EXPECT_TRUE(ConfigureBackend(MakeArdaTestBackendConfiguration()));
				ConfigureShaderCompiler(mPreviousCompiler);
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		};

		TEST_P(FArdaGraphComputeAlternativeGpu, NodeOwnedOutputUsesComputeAndReadbackWithoutCudaStorage)
		{
			FArdaGraphAlternativeNode::mComputeRecordCount = FArdaGraphAlternativeNode::mCudaRecordCount = 0;
			FArdaDependencyGraph Graph(GetDevice());
			for (uint32_t Frame = 0; Frame < 3; ++Frame)
			{
				FArdaGraphEdit Edit(Graph);
				const auto Operation =
				    FArdaGraphAlternativeNode{}(Edit, FArdaGraphAlternativeNodeParameters{}, "alternative");
				ASSERT_TRUE(Operation) << Edit.GetStatus().mMessage.c_str();
				const auto Output = Operation.GetBuffer("Output");
				ASSERT_TRUE(Output);
				ASSERT_NE(Graph.FindResource(Output), nullptr);
				EXPECT_FALSE(Graph.FindResource(Output)->mBuffer.mbCudaInterop);
				const auto Read = Readback(Edit, Output);
				ASSERT_TRUE(Read);
				ASSERT_TRUE(Edit.Commit());
				const auto Compiled = Graph.CompileAndWait();
				ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
				EXPECT_TRUE(Graph.GetCompileResult().mCudaBatches.empty());
				const auto Executed = Graph.ExecuteAndWait();
				ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
				ASSERT_EQ(Read->size(), 257u * sizeof(uint32_t));
				for (uint32_t Index = 0; Index < 257; ++Index)
				{
					uint32_t Actual;
					std::memcpy(&Actual, Read->data() + Index * sizeof(uint32_t), sizeof(Actual));
					EXPECT_EQ(Actual, Index * 3 + 7) << "frame=" << Frame << " index=" << Index;
				}
			}
			EXPECT_EQ(FArdaGraphAlternativeNode::mComputeRecordCount, 3u);
			EXPECT_EQ(FArdaGraphAlternativeNode::mCudaRecordCount, 0u);
		}

#if defined(ARDA_TEST_NATIVE_D3D12) || defined(ARDA_TEST_NATIVE_VULKAN)
		const char* const ArdaAlternativeBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
		    "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
		    "native-vulkan",
#endif
		};
		INSTANTIATE_TEST_SUITE_P(Native, FArdaGraphComputeAlternativeGpu, testing::ValuesIn(ArdaAlternativeBackends));
#else
		GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(FArdaGraphComputeAlternativeGpu);
#endif
	}
}
