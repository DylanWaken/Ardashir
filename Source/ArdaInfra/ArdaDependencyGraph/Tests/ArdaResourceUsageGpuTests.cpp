#include "ArdaBackend.h"
#include "ArdaTestBackend.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaGraphicsOperations.h"
#include "Graph/Node/ArdaNodeDefinition.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"
#include "RHI/Shaders/ArdaShaderCompiler.h"
#include <gtest/gtest.h>
#include <cstring>

namespace arda::resource_usage_gpu_tests
{
	class FArdaUsageShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mInput, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(EArdaShaderParameterKind::BufferUAV,
			    EArdaRHIBindingType::RawBufferUAV,
			    FArdaRHIBufferRef,
			    mOutput,
			    0,
			    0,
			    EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaUsageShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaUsageShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaResourceUsageGpu.hlsl",
	    "ResourceUsageDeduction",
	    "UsageCS",
	    EArdaRHIShaderStage::Compute)

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaUsageComputeParameters)
		ARDA_NODE_SHADER_BUFFER(mInput, FArdaUsageShader::FArdaParameters, mInput)
		ARDA_NODE_SHADER_BUFFER(mOutput, FArdaUsageShader::FArdaParameters, mOutput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaUsageComputeNode final
	    : public TArdaNodeDefinition<FArdaUsageComputeNode, FArdaUsageComputeParameters, EArdaNodeType::Compute>
	{
	public:
		struct FArdaState
		{
			FArdaGlobalShaderMap mShaders;
			FArdaInductorPipelineContribution mStage;
		};

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.resource-usage.compute", 1};
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			auto State = eastl::make_shared<FArdaState>();
			if (!State->mShaders.Initialize(Device))
			{
				const auto Diagnostics = State->mShaders.GetDiagnostics();
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				        Diagnostics.empty() ? "Usage shader initialization failed."
				                            : Diagnostics.back().mMessage.c_str())};
			}
			const auto* Shader = State->mShaders.Find(FArdaUsageShader::GetStaticType());
			if (!Shader)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Usage shader missing.")};
			}
			State->mStage.mShader = Shader->GetShader();
			State->mStage.mBindingLayouts = Shader->GetBindingLayouts();
			return {eastl::move(State), {}};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState& State)
		{
			FArdaDependencyNodeDesc D;
			D.mPipelineStages = {State.mStage};
			FArdaInductorPipelineRequest Request;
			Request.mKind = EArdaPipelineStateKind::Compute;
			D.mPipelines = {Request};
			return D;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			if (auto Status = Context.SetComputeState(); !Status)
			{
				return Status;
			}
			Context.GetCommands().Dispatch(1, 1, 1);
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaUsageComputeNode, ComputeResourceUsage);
	ARDA_DEFINE_GRAPH_NODE(FArdaUsageComputeNode, ComputeResourceUsage);

	class FArdaUsageVertexShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mPositions, 0, 0, EArdaRHIShaderStage::Vertex)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaUsageVertexShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaUsageVertexShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaResourceUsageGraphics.hlsl",
	    "ResourceUsageVertex",
	    "UsageVS",
	    EArdaRHIShaderStage::Vertex)

	class FArdaUsagePixelShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaUsagePixelShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaUsagePixelShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaResourceUsageGraphics.hlsl",
	    "ResourceUsagePixel",
	    "UsagePS",
	    EArdaRHIShaderStage::Pixel)

	template <class Shader>
	TArdaRHIResult<eastl::shared_ptr<const FArdaUsageComputeNode::FArdaState>> PrepareUsageGraphicsShader(
	    FArdaRHIDeviceRef Device)
	{
		auto State = eastl::make_shared<FArdaUsageComputeNode::FArdaState>();
		if (!State->mShaders.Initialize(Device))
		{
			const auto Diagnostics = State->mShaders.GetDiagnostics();
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			        Diagnostics.empty() ? "Usage graphics shader initialization failed."
			                            : Diagnostics.back().mMessage.c_str())};
		}
		const auto* Compiled = State->mShaders.Find(Shader::GetStaticType());
		if (!Compiled)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Usage graphics shader missing.")};
		}
		State->mStage.mShader = Compiled->GetShader();
		State->mStage.mBindingLayouts = Compiled->GetBindingLayouts();
		return {eastl::move(State), {}};
	}

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaUsageVertexParameters)
		ARDA_NODE_SHADER_BUFFER(mPositions, FArdaUsageVertexShader::FArdaParameters, mPositions)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaUsageVertexNode final
	    : public TArdaNodeDefinition<FArdaUsageVertexNode, FArdaUsageVertexParameters, EArdaNodeType::Vertex>
	{
	public:
		using FArdaState = FArdaUsageComputeNode::FArdaState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.resource-usage.vertex", 1};
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			return PrepareUsageGraphicsShader<FArdaUsageVertexShader>(eastl::move(Device));
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState& State)
		{
			FArdaDependencyNodeDesc D;
			D.mPipelineStages = {State.mStage};
			return D;
		}

		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaGraphicsWork& Work)
		{
			Work.mDraw.mVertexCount = 3;
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaUsageVertexNode, ResourceUsageVertices);
	ARDA_DEFINE_GRAPH_NODE(FArdaUsageVertexNode, ResourceUsageVertices);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaUsagePixelParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)
		ARDA_NODE_COLOR_TARGET(mColor)
		ARDA_NODE_DEPTH_TARGET(mDepth, EArdaDependencyAccess::Write)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaUsagePixelNode final
	    : public TArdaNodeDefinition<FArdaUsagePixelNode, FArdaUsagePixelParameters, EArdaNodeType::Pixel>
	{
	public:
		using FArdaState = FArdaUsageComputeNode::FArdaState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.resource-usage.pixel", 1};
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			return PrepareUsageGraphicsShader<FArdaUsagePixelShader>(eastl::move(Device));
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState& State)
		{
			FArdaDependencyNodeDesc D;
			D.mPipelineStages = {State.mStage};
			return D;
		}

		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaGraphicsWork&)
		{
			if (auto Status =
			        Context.GetCommands().ClearTexture(*Context.GetTexture(P.mColor), {}, {1.f, 0.f, 0.f, 1.f});
			    !Status)
			{
				return Status;
			}
			return Context.GetCommands()
			    .ClearDepthStencilTexture(*Context.GetTexture(P.mDepth), {}, true, 0.f, false, 0);
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaUsagePixelNode, ResourceUsagePixels);
	ARDA_DEFINE_GRAPH_NODE(FArdaUsagePixelNode, ResourceUsagePixels);

	class FArdaResourceUsageGpu : public testing::TestWithParam<const char*>
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
			if (!FindBackendModule(GetParam()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mMessageCallback = &mDiagnostics;
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::OnDemand;
			Configuration.mShaderCacheDirectory = ARDA_BACKEND_TEST_SHADER_DIR;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
		}

		void TearDown() override
		{
			const bool MissingValidation = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (auto Device = GetDevice())
			{
				EXPECT_TRUE(Device->WaitForIdle());
			}
			ShutdownBackend();
			ConfigureShaderCompiler(mPreviousCompiler);
			if (!MissingValidation)
			{
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}
	};

	TEST_P(FArdaResourceUsageGpu, ShaderMetadataDeducesStructuredSrvAndRawUavAndExecutes)
	{
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc InputDesc;
		InputDesc.mByteSize = 4 * sizeof(uint32_t);
		InputDesc.mStructureStride = sizeof(uint32_t);
		const auto Input = AllocateBuffer(Edit, InputDesc);
		FArdaRHIBufferDesc OutputDesc;
		OutputDesc.mByteSize = InputDesc.mByteSize;
		const auto Output = AllocateBuffer(Edit, OutputDesc);
		const uint32_t Values[] = {1, 19, 37, 91};
		eastl::vector<uint8_t> Bytes(sizeof(Values));
		std::memcpy(Bytes.data(), Values, sizeof(Values));
		ASSERT_TRUE(Upload(Edit, Input, eastl::move(Bytes)));
		ASSERT_TRUE(ComputeResourceUsage(Edit, Input, Output)) << Edit.GetStatus().mMessage.c_str();
		const auto Read = Readback(Edit, Output);
		ASSERT_TRUE(Read);
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		EXPECT_EQ(Graph.FindResource(Input)->mBuffer.mUsage,
		    EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource);
		EXPECT_EQ(Graph.FindResource(Output)->mBuffer.mUsage,
		    EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::UnorderedAccess);
		const auto Executed = Graph.Execute();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_EQ(Read->size(), sizeof(Values));
		for (uint32_t Index = 0; Index < 4; ++Index)
		{
			uint32_t Actual = 0;
			std::memcpy(&Actual, Read->data() + Index * sizeof(Actual), sizeof(Actual));
			EXPECT_EQ(Actual, Values[Index] * 3 + 7);
		}
	}

	TEST_P(FArdaResourceUsageGpu, AutomaticClearBufferAndColorTextureRoundTrip)
	{
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 64;
		const auto Buffer = AllocateBuffer(Edit, BufferDesc);
		ASSERT_TRUE(Clear(Edit, Buffer, 0x12345678));
		const auto BufferBytes = Readback(Edit, Buffer);
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = 5;
		TextureDesc.mHeight = 3;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		const auto Texture = AllocateTexture(Edit, TextureDesc);
		ASSERT_TRUE(Clear(Edit, Texture, {1.f, 0.f, 1.f, 1.f}));
		const auto TextureBytes = Readback(Edit, Texture);
		ASSERT_TRUE(BufferBytes);
		ASSERT_TRUE(TextureBytes);
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, EArdaRHIBufferUsage::UnorderedAccess);
		EXPECT_EQ(Graph.FindResource(Texture)->mTexture.mUsage, EArdaRHITextureUsage::RenderTarget);
		const auto Executed = Graph.Execute();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_EQ(BufferBytes->size(), 64u);
		for (size_t Offset = 0; Offset < BufferBytes->size(); Offset += sizeof(uint32_t))
		{
			uint32_t Value = 0;
			std::memcpy(&Value, BufferBytes->data() + Offset, sizeof(Value));
			EXPECT_EQ(Value, 0x12345678u);
		}
		ASSERT_EQ(TextureBytes->size(), 5u * 3u * 4u);
		for (size_t Offset = 0; Offset < TextureBytes->size(); Offset += 4)
		{
			EXPECT_EQ((*TextureBytes)[Offset], 255u);
			EXPECT_EQ((*TextureBytes)[Offset + 1], 0u);
			EXPECT_EQ((*TextureBytes)[Offset + 2], 255u);
			EXPECT_EQ((*TextureBytes)[Offset + 3], 255u);
		}
	}

	TEST_P(FArdaResourceUsageGpu, ImportedBufferRetainsItsNativeCapabilities)
	{
		FArdaRHIBufferDesc InputDesc;
		InputDesc.mByteSize = 16;
		InputDesc.mStructureStride = 4;
		InputDesc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource;
		const auto NativeInput = GetDevice()->CreateBuffer(InputDesc);
		ASSERT_TRUE(NativeInput);
		FArdaRHIBufferDesc OutputDesc;
		OutputDesc.mByteSize = 16;
		OutputDesc.mUsage = EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::ShaderResource;
		const auto NativeOutput = GetDevice()->CreateBuffer(OutputDesc);
		ASSERT_TRUE(NativeOutput);
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		const auto Input = BindBuffer(Edit, NativeInput.mValue);
		const auto Output = BindBuffer(Edit, NativeOutput.mValue);
		ASSERT_TRUE(ComputeResourceUsage(Edit, Input, Output)) << Edit.GetStatus().mMessage.c_str();
		Edit.Keep(Output);
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(Graph.Compile());
		EXPECT_EQ(NativeOutput.mValue->GetDesc().mUsage, OutputDesc.mUsage);
	}

	TEST_P(FArdaResourceUsageGpu, AssembledGraphicsStagesDeduceVertexSrvColorAndDepthUsage)
	{
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		const float Positions[3][4] = {{-1.f, -1.f, 0.5f, 1.f}, {3.f, -1.f, 0.5f, 1.f}, {-1.f, 3.f, 0.5f, 1.f}};
		FArdaRHIBufferDesc PositionDesc;
		PositionDesc.mByteSize = sizeof(Positions);
		PositionDesc.mStructureStride = sizeof(Positions[0]);
		const auto PositionBuffer = AllocateBuffer(Edit, PositionDesc);
		eastl::vector<uint8_t> Bytes(sizeof(Positions));
		std::memcpy(Bytes.data(), Positions, sizeof(Positions));
		ASSERT_TRUE(Upload(Edit, PositionBuffer, eastl::move(Bytes)));
		FArdaRHITextureDesc TargetDesc;
		TargetDesc.mWidth = TargetDesc.mHeight = 4;
		TargetDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		const auto Color = AllocateTexture(Edit, TargetDesc);
		TargetDesc.mFormat = EArdaRHIFormat::D32;
		const auto Depth = AllocateTexture(Edit, TargetDesc);
		ASSERT_TRUE(Color);
		ASSERT_TRUE(Depth);
		const auto Vertices = ResourceUsageVertices(Edit, PositionBuffer);
		ASSERT_TRUE(Vertices) << Edit.GetStatus().mMessage.c_str();
		FArdaRasterConfig Config;
		Config.mViewport = {0.f, 4.f, 0.f, 4.f, 0.f, 1.f};
		Config.mRasterState.mCullMode = EArdaRHICullMode::None;
		Config.mDepthStencilState.mbDepthTest = Config.mDepthStencilState.mbDepthWrite = true;
		const auto Fragments = Rasterize(Edit, Vertices, Config);
		ASSERT_TRUE(Fragments);
		ASSERT_TRUE(ResourceUsagePixels(Edit, Fragments, Color, Depth));
		const auto Read = Readback(Edit, Color);
		ASSERT_TRUE(Read);
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		EXPECT_EQ(Graph.FindResource(PositionBuffer)->mBuffer.mUsage,
		    EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource);
		EXPECT_EQ(Graph.FindResource(Color)->mTexture.mUsage, EArdaRHITextureUsage::RenderTarget);
		EXPECT_EQ(Graph.FindResource(Depth)->mTexture.mUsage, EArdaRHITextureUsage::DepthStencil);
		const auto Executed = Graph.Execute();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_EQ(Read->size(), 4u * 4u * 4u);
		for (size_t Offset = 0; Offset < Read->size(); Offset += 4)
		{
			EXPECT_EQ((*Read)[Offset], 0u);
			EXPECT_EQ((*Read)[Offset + 1], 255u);
			EXPECT_EQ((*Read)[Offset + 2], 0u);
			EXPECT_EQ((*Read)[Offset + 3], 255u);
		}
	}

#if defined(ARDA_TEST_NATIVE_D3D12) || defined(ARDA_TEST_NATIVE_VULKAN)
	const char* const NativeBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
	    "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
	    "native-vulkan",
#endif
	};
	INSTANTIATE_TEST_SUITE_P(Native, FArdaResourceUsageGpu, testing::ValuesIn(NativeBackends));
#else
	GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(FArdaResourceUsageGpu);
#endif
}
