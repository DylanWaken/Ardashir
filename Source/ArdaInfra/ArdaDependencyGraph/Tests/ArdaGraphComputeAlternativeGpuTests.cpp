#include "ArdaBackend.h"
#include "ArdaTestBackend.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaNodeDefinition.h"
#include "Inductor/Runtime/ArdaDependencyGraphCuda.h"
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
				if (auto Status = Context.SetComputeState(); !Status)
				{
					return Status;
				}
				Context.GetCommands().Dispatch((Parameters.mCount + 63) / 64, 1, 1);
				return {};
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

		class FArdaLegacyGraphAlternativeOperand final
		    : public TArdaComputeOperand<FArdaGraphAlternativeCudaParameters, uint32_t>
		{
		public:
			using TArdaComputeOperand::TArdaComputeOperand;
			mutable uint32_t mComputePreparations = 0, mNativeSelections = 0;
			bool mbFailPreparation = false;

			const char* GetName() const noexcept override
			{
				return "test.legacy.graph-alternative";
			}

			void BindKernelVariants(FArdaRegistry&) const override
			{
			}

			bool HasComputeAlternative() const noexcept override
			{
				return true;
			}

			FArdaRHIStatus CheckComputeAlternativeSupport() const override
			{
				return {};
			}

			TArdaRHIResult<FArdaCudaKernelSelection> SelectKernel(const FArdaParameters&,
			    const FArdaCudaSelectionContext&,
			    const FArdaVariants&) const override
			{
				++mNativeSelections;
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Native selection must not run.")};
			}

			TArdaRHIResult<FArdaComputeAlternativePlan> PrepareComputeAlternative(const FArdaParameters& Parameters,
			    EArdaRHIQueueType) const override
			{
				++mComputePreparations;
				if (!Parameters.mOutput.mBuffer || Parameters.mOutput.mBuffer->GetDesc().mbCudaInterop ||
				    Parameters.mOutput.mBuffer->GetDesc().mByteSize != 64 || Parameters.mOutput.mRange.mByteSize != 64)
				{
					return {{},
					    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					        "Logical arguments did not resolve to the declared non-CUDA buffer and range.")};
				}
				if (mbFailPreparation)
				{
					return {{},
					    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Intentional alternative failure.")};
				}
				FArdaComputeAlternativePlan Plan;
				Plan.mbNoWork = true;
				return {Plan, {}};
			}
		};

		struct FArdaLegacyGraphAlternativeNode
		    : TArdaCudaOperandNode<FArdaLegacyGraphAlternativeNode, FArdaLegacyGraphAlternativeOperand>
		{
			using FArdaBase = TArdaCudaOperandNode<FArdaLegacyGraphAlternativeNode, FArdaLegacyGraphAlternativeOperand>;

			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.graph.legacy-compute-alternative", 1};
			}

			static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State)
			{
				auto Description = FArdaBase::Describe(Parameters, State);
				Description.mbSideEffect = true;
				return Description;
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
				(void)FArdaNodeRegistry::Get().Unregister(FArdaLegacyGraphAlternativeNode::GetMetadata().mName);
				ShutdownBackend();
				EXPECT_TRUE(ConfigureBackend(MakeArdaTestBackendConfiguration()));
				ConfigureShaderCompiler(mPreviousCompiler);
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		};

		TEST_P(FArdaGraphComputeAlternativeGpu,
		    LegacyOperandAlternativeResolvesArgumentsAndPropagatesPreparationFailure)
		{
			auto Operand = eastl::make_shared<FArdaLegacyGraphAlternativeOperand>(GetDevice());
			FArdaDependencyGraph Graph(GetDevice());
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc Description;
			Description.mByteSize = 64;
			Description.mStructureStride = sizeof(uint32_t);
			Description.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
			const auto Output = AllocateBuffer(Edit, Description, "output");
			ASSERT_TRUE(Output);
			FArdaLegacyGraphAlternativeNode::FArdaParameters Parameters;
			Parameters.mOperation = Operand;
			Parameters.mArguments.mOutput = {Output.GetHandle(), {0, 64}};
			ASSERT_TRUE(FArdaLegacyGraphAlternativeNode::CheckRequiredFeatures(GetDevice().Get(), Parameters));
			ASSERT_TRUE(FArdaLegacyGraphAlternativeNode{}(Edit, Parameters, "legacy alternative"));
			ASSERT_TRUE(Edit.Commit());
			const auto Compiled = Graph.Compile();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			EXPECT_TRUE(Graph.GetCompileResult().mCudaBatches.empty());
			const auto Executed = Graph.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(Operand->mComputePreparations, 1u);
			EXPECT_EQ(Operand->mNativeSelections, 0u);
			Operand->mbFailPreparation = true;
			const auto Failed = Graph.Execute();
			EXPECT_EQ(Failed.mStatus.mCode, EArdaRHIResult::BackendFailure);
			EXPECT_NE(Failed.mStatus.mMessage.find("Intentional alternative failure"), eastl::string::npos);
			EXPECT_EQ(Operand->mComputePreparations, 2u);
			EXPECT_EQ(Operand->mNativeSelections, 0u);
		}

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
				const auto Compiled = Graph.Compile();
				ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
				EXPECT_TRUE(Graph.GetCompileResult().mCudaBatches.empty());
				const auto Executed = Graph.Execute();
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
