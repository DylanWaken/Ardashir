#include "ArdaGraphTestNodeTypes.h"
#include "ArdaBackend.h"
#include "ArdaTestBackend.h"
#include "Graph/DefaultNodes/ArdaGraphicsOperations.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaGraphicsStageNode.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Rules/ArdaInductorRequirements.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"
#include "RHI/Shaders/ArdaShaderCompiler.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>

namespace arda::graphics_stage_gpu_tests
{
	struct FArdaVertex
	{
		float mPosition[4];
		float mColor[4];
	};

	class FArdaStageVertexShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mInput, 0, 0, EArdaRHIShaderStage::Vertex)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaStageVertexShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaStageVertexShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphicsStageCapture.hlsl",
	    "StageCaptureVertex",
	    "StageVS",
	    EArdaRHIShaderStage::Vertex)

	class FArdaStagePixelShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mInput, 0, 0, EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaStagePixelShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaStagePixelShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphicsStageCapture.hlsl",
	    "StageCapturePixel",
	    "StagePS",
	    EArdaRHIShaderStage::Pixel)

	class FArdaVertexCaptureShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mInput, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mOutput, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_PUSH_CONSTANTS(uint32_t, mCapacity, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaVertexCaptureShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaVertexCaptureShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphicsStageCapture.hlsl",
	    "StageCaptureVertices",
	    "CaptureVerticesCS",
	    EArdaRHIShaderStage::Compute)

	class FArdaFragmentCaptureShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_UAV(mOutput, 1, 0, EArdaRHIShaderStage::Pixel)
			ARDA_SHADER_PUSH_CONSTANTS(uint32_t, mCapacity, 0, 0, EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaFragmentCaptureShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaFragmentCaptureShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphicsStageCapture.hlsl",
	    "StageCaptureFragments",
	    "CaptureFragmentsPS",
	    EArdaRHIShaderStage::Pixel)

	class FArdaStageGeometryShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaStageGeometryShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaStageGeometryShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphicsStageExpanded.hlsl",
	    "StageExpandedGeometry",
	    "StageGS",
	    EArdaRHIShaderStage::Geometry)

	class FArdaStageHullShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaStageHullShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaStageHullShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphicsStageExpanded.hlsl",
	    "StageExpandedHull",
	    "StageHS",
	    EArdaRHIShaderStage::Hull)

	class FArdaStageDomainShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaStageDomainShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaStageDomainShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphicsStageExpanded.hlsl",
	    "StageExpandedDomain",
	    "StageDS",
	    EArdaRHIShaderStage::Domain)

	class FArdaStageAmplificationShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mInput, 0, 0, EArdaRHIShaderStage::Amplification)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaStageAmplificationShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaStageAmplificationShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphicsStageExpanded.hlsl",
	    "StageExpandedAmplification",
	    "StageAS",
	    EArdaRHIShaderStage::Amplification)

	class FArdaStageMeshShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mInput, 0, 0, EArdaRHIShaderStage::Mesh)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaStageMeshShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaStageMeshShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphicsStageExpanded.hlsl",
	    "StageExpandedMesh",
	    "StageMS",
	    EArdaRHIShaderStage::Mesh)

	class FArdaStagePayloadMeshShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mInput, 0, 0, EArdaRHIShaderStage::Mesh)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaStagePayloadMeshShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaStagePayloadMeshShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaGraphicsStageExpanded.hlsl",
	    "StageExpandedPayloadMesh",
	    "StagePayloadMS",
	    EArdaRHIShaderStage::Mesh)

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaVertexStorageParameters)
		ARDA_SHADER_BUFFER_UAV(mOutput, 0, 0, EArdaRHIShaderStage::Vertex)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	TEST(ArdaGraphicsStageStorage, ShaderSchemasInferPixelStorageAdmissionBeforePhysicalBindingAssembly)
	{
		FArdaDependencyGraph::FArdaImpl Graph;
		FArdaDependencyNodeDesc Pixel;
		Pixel.mGraphicsStage = EArdaGraphicsStage::Pixel;
		Pixel.BindShader<FArdaFragmentCaptureShader::FArdaParameters>({});
		InferArdaNodeRequirements(Graph, EArdaDependencyNodeKind::Graphics, Pixel);
		EXPECT_TRUE(Pixel.mRequirements.mFeatures.mbRequireFragmentShaderStoresAndAtomics);
		EXPECT_FALSE(Pixel.mRequirements.mFeatures.mbRequireVertexShaderStoresAndAtomics);
		FArdaRHICapabilities Caps;
		EXPECT_FALSE(Caps.Evaluate(Pixel.mRequirements.mFeatures).IsSupported());
		Caps.mbFragmentShaderStoresAndAtomics = true;
		EXPECT_TRUE(Caps.Evaluate(Pixel.mRequirements.mFeatures).IsSupported());

		FArdaDependencyNodeDesc Vertex;
		Vertex.mGraphicsStage = EArdaGraphicsStage::Vertex;
		Vertex.BindShader<FArdaVertexStorageParameters>({});
		InferArdaNodeRequirements(Graph, EArdaDependencyNodeKind::Graphics, Vertex);
		EXPECT_FALSE(Vertex.mRequirements.mFeatures.mbRequireFragmentShaderStoresAndAtomics);
		EXPECT_TRUE(Vertex.mRequirements.mFeatures.mbRequireVertexShaderStoresAndAtomics);
		EXPECT_FALSE(Caps.Evaluate(Vertex.mRequirements.mFeatures).IsSupported());
		Caps.mbVertexShaderStoresAndAtomics = true;
		EXPECT_TRUE(Caps.Evaluate(Vertex.mRequirements.mFeatures).IsSupported());

		FArdaDependencyNodeDesc Compute;
		Compute.BindShader<FArdaVertexCaptureShader::FArdaParameters>({});
		InferArdaNodeRequirements(Graph, EArdaDependencyNodeKind::Compute, Compute);
		EXPECT_FALSE(Compute.mRequirements.mFeatures.mbRequireFragmentShaderStoresAndAtomics);
		EXPECT_FALSE(Compute.mRequirements.mFeatures.mbRequireVertexShaderStoresAndAtomics);
	}

	struct FArdaShaderState
	{
		FArdaGlobalShaderMap mShaders;
		FArdaInductorPipelineContribution mStage;
		FArdaRHIInputLayoutRef mInputLayout;
	};

	template <class Shader>
	TArdaRHIResult<eastl::shared_ptr<const FArdaShaderState>> PrepareShader(FArdaRHIDeviceRef Device,
	    FArdaRHIInputLayoutRef InputLayout = {})
	{
		auto State = eastl::make_shared<FArdaShaderState>();
		if (!State->mShaders.Initialize(Device))
		{
			const auto Errors = State->mShaders.GetDiagnostics();
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			        Errors.empty() ? "Stage fixture shader initialization failed." : Errors.back().mMessage.c_str())};
		}
		const auto* Compiled = State->mShaders.Find(Shader::GetStaticType());
		if (!Compiled)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Stage fixture shader is missing.")};
		}
		State->mStage.mShader = Compiled->GetShader();
		State->mStage.mBindingLayouts = Compiled->GetBindingLayouts();
		State->mInputLayout = eastl::move(InputLayout);
		return {eastl::move(State), {}};
	}

	FArdaDependencyShaderValue CapacityValue(uint32_t Capacity)
	{
		FArdaDependencyShaderValue Value;
		Value.mMember = "mCapacity";
		Value.mBytes.resize(sizeof(Capacity));
		std::memcpy(Value.mBytes.data(), &Capacity, sizeof(Capacity));
		return Value;
	}

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaVertexCaptureParameters)
		ARDA_NODE_SHADER_BUFFER(mInput, FArdaVertexCaptureShader::FArdaParameters, mInput)
		ARDA_NODE_SHADER_BUFFER(mOutput, FArdaVertexCaptureShader::FArdaParameters, mOutput)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mCount, 3)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbKeepDispatch, false)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaVertexCaptureNode final
	    : public TArdaNodeDefinition<FArdaVertexCaptureNode, FArdaVertexCaptureParameters, EArdaNodeType::Compute>
	{
	public:
		using FArdaState = FArdaShaderState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.graphics-stage.capture-vertices", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}
			    .Resource(P.mInput)
			    .Resource(P.mOutput)
			    .Value(P.mCount)
			    .Value(P.mbKeepDispatch)
			    .Build();
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			return PrepareShader<FArdaVertexCaptureShader>(eastl::move(Device));
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.BindShader<FArdaVertexCaptureShader::FArdaParameters>({}, "default", {CapacityValue(P.mCount)});
			Desc.mbSideEffect = P.mbKeepDispatch;
			Desc.mPipelineStages = {State.mStage};
			Desc.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute}};
			return Desc;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			if (auto Status = Context.SetComputeState(); !Status)
			{
				return Status;
			}
			Context.GetCommands().Dispatch((P.mCount + 63) / 64, 1, 1);
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaVertexCaptureNode, CaptureStageVertices);
	ARDA_DEFINE_GRAPH_NODE(FArdaVertexCaptureNode, CaptureStageVertices);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaVertexStageParameters)
		ARDA_NODE_SHADER_BUFFER(mVertices, FArdaStageVertexShader::FArdaParameters, mInput)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mCount, 3)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaVertexStageNode final
	    : public TArdaNodeDefinition<FArdaVertexStageNode, FArdaVertexStageParameters, EArdaNodeType::Vertex>
	{
	public:
		using FArdaState = FArdaShaderState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.graphics-stage.vertex", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mVertices).Value(P.mCount).Build();
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			return PrepareShader<FArdaStageVertexShader>(eastl::move(Device));
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelineStages = {State.mStage};
			return Desc;
		}

		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext&,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaGraphicsWork& Work)
		{
			Work.mDraw.mVertexCount = P.mCount;
			return {};
		}

		static FArdaGraphBuffer FetchBuffer(FArdaGraphEdit& Edit,
		    const FArdaParameters& P,
		    const FArdaStageBufferRequest& Request)
		{
			const uint64_t Count = Request.mElementCount ? Request.mElementCount : P.mCount;
			if (!Count || Count > P.mCount)
			{
				Edit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Vertex capture count must fit the producer's vertex count."));
				return {};
			}
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = Count * sizeof(FArdaVertex);
			Desc.mStructureStride = sizeof(FArdaVertex);
			Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess |
			    EArdaRHIBufferUsage::ShaderResource;
			auto Output = AllocateBuffer(Edit, Desc);
			CaptureStageVertices(Edit, P.mVertices, Output, uint32_t(Count));
			return Output;
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaVertexStageNode, StageVertices);
	ARDA_DEFINE_GRAPH_NODE(FArdaVertexStageNode, StageVertices);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGeometryStageParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphVertices, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaHullStageParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphVertices, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDomainStageParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphControlPoints, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaAmplificationStageParameters)
		ARDA_NODE_SHADER_BUFFER(mVertices, FArdaStageAmplificationShader::FArdaParameters, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMeshStageParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphMeshPayload, mInput)
		ARDA_NODE_SHADER_BUFFER(mVertices, FArdaStageMeshShader::FArdaParameters, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaPayloadMeshStageParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphMeshPayload, mInput)
		ARDA_NODE_SHADER_BUFFER(mVertices, FArdaStagePayloadMeshShader::FArdaParameters, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	template <class Shader, class Parameters, EArdaGraphicsStage Stage>
	class TArdaAdditionalShaderNode final
	    : public TArdaNodeDefinition<TArdaAdditionalShaderNode<Shader, Parameters, Stage>,
	          Parameters,
	          ArdaTestNodeType(Stage)>
	{
	public:
		using FArdaParameters = Parameters;
		using FArdaState = FArdaShaderState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {eastl::string("test.graphics-stage.") + Shader::GetStaticType().GetName(), 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			if constexpr (Stage == EArdaGraphicsStage::Amplification || Stage == EArdaGraphicsStage::Mesh)
			{
				return FArdaDependencyKeyBuilder{}.Resource(P.mVertices).Build();
			}
			return {};
		}

		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters&)
		{
			FArdaDependencyNodeRequirements R;
			R.mFeatures.mbRequireGeometryShaders = Stage == EArdaGraphicsStage::Geometry;
			R.mFeatures.mbRequireTessellationShaders =
			    Stage == EArdaGraphicsStage::Hull || Stage == EArdaGraphicsStage::Domain;
			R.mFeatures.mbRequireMeshShaders =
			    Stage == EArdaGraphicsStage::Mesh || Stage == EArdaGraphicsStage::Amplification;
			if constexpr (Stage == EArdaGraphicsStage::Amplification)
			{
				R.mFeatures.mMinMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
			}
			return R;
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			return PrepareShader<Shader>(eastl::move(Device));
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelineStages = {State.mStage};
			return Desc;
		}
	};

	using FArdaTestGeometryStageNode =
	    TArdaAdditionalShaderNode<FArdaStageGeometryShader, FArdaGeometryStageParameters, EArdaGraphicsStage::Geometry>;
	using TArdaHullTestStageNode =
	    TArdaAdditionalShaderNode<FArdaStageHullShader, FArdaHullStageParameters, EArdaGraphicsStage::Hull>;
	using TArdaDomainTestStageNode =
	    TArdaAdditionalShaderNode<FArdaStageDomainShader, FArdaDomainStageParameters, EArdaGraphicsStage::Domain>;
	using TArdaAmplificationTestStageNode = TArdaAdditionalShaderNode<FArdaStageAmplificationShader,
	    FArdaAmplificationStageParameters,
	    EArdaGraphicsStage::Amplification>;
	using TArdaMeshTestStageNode =
	    TArdaAdditionalShaderNode<FArdaStageMeshShader, FArdaMeshStageParameters, EArdaGraphicsStage::Mesh>;
	using TArdaPayloadMeshTestStageNode = TArdaAdditionalShaderNode<FArdaStagePayloadMeshShader,
	    FArdaPayloadMeshStageParameters,
	    EArdaGraphicsStage::Mesh>;
	ARDA_DECLARE_GRAPH_NODE(FArdaTestGeometryStageNode, StageGeometry);
	ARDA_DEFINE_GRAPH_NODE(FArdaTestGeometryStageNode, StageGeometry);
	ARDA_DECLARE_GRAPH_NODE(TArdaHullTestStageNode, StageHull);
	ARDA_DEFINE_GRAPH_NODE(TArdaHullTestStageNode, StageHull);
	ARDA_DECLARE_GRAPH_NODE(TArdaDomainTestStageNode, StageDomain);
	ARDA_DEFINE_GRAPH_NODE(TArdaDomainTestStageNode, StageDomain);
	ARDA_DECLARE_GRAPH_NODE(TArdaAmplificationTestStageNode, StageAmplify);
	ARDA_DEFINE_GRAPH_NODE(TArdaAmplificationTestStageNode, StageAmplify);
	ARDA_DECLARE_GRAPH_NODE(TArdaMeshTestStageNode, StageMesh);
	ARDA_DEFINE_GRAPH_NODE(TArdaMeshTestStageNode, StageMesh);
	ARDA_DECLARE_GRAPH_NODE(TArdaPayloadMeshTestStageNode, StagePayloadMesh);
	ARDA_DEFINE_GRAPH_NODE(TArdaPayloadMeshTestStageNode, StagePayloadMesh);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaRasterStageParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphVertices, mInput)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mWidth, 8)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mHeight, 8)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(EArdaPipelineStateKind, mFamily, EArdaPipelineStateKind::Graphics)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbTessellation, false)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaRasterStageNode final
	    : public TArdaNodeDefinition<FArdaRasterStageNode, FArdaRasterStageParameters, EArdaNodeType::Raster>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.graphics-stage.raster", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}
			    .Value(P.mWidth)
			    .Value(P.mHeight)
			    .Value(P.mFamily)
			    .Value(P.mbTessellation)
			    .Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = P.mFamily;
			Configuration->mGraphics.mDesc.mRasterState.mCullMode = EArdaRHICullMode::None;
			Configuration->mGraphics.mDesc.mDepthStencilState.mbDepthTest = false;
			Configuration->mGraphics.mDesc.mDepthStencilState.mbDepthWrite = false;
			Configuration->mMeshlet.mDesc.mRasterState.mCullMode = EArdaRHICullMode::None;
			Configuration->mMeshlet.mDesc.mDepthStencilState.mbDepthTest = false;
			Configuration->mMeshlet.mDesc.mDepthStencilState.mbDepthWrite = false;
			if (P.mbTessellation)
			{
				Configuration->mGraphics.mDesc.mTopology = EArdaRHIPrimitiveTopology::PatchList;
				Configuration->mGraphics.mDesc.mPatchControlPoints = 3;
			}
			FArdaInductorPipelineContribution Contribution;
			Contribution.mConfiguration = eastl::move(Configuration);
			Desc.mPipelineStages = {eastl::move(Contribution)};
			return Desc;
		}

		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext&,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaGraphicsWork& Work)
		{
			Work.mState.mViewports = {{0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f}};
			Work.mState.mScissors = {{0, int32_t(P.mWidth), 0, int32_t(P.mHeight)}};
			Work.mMeshletState.mViewports = Work.mState.mViewports;
			Work.mMeshletState.mScissors = Work.mState.mScissors;
			return {};
		}

		static FArdaGraphBuffer FetchBuffer(FArdaGraphEdit& Edit,
		    const FArdaParameters& P,
		    const FArdaStageBufferRequest& Request);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaRasterStageNode, StageRasterize);
	ARDA_DEFINE_GRAPH_NODE(FArdaRasterStageNode, StageRasterize);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaPixelStageParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)
		ARDA_NODE_COLOR_TARGET(mColor)
		ARDA_NODE_SHADER_BUFFER(mVertices, FArdaStagePixelShader::FArdaParameters, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaPixelStageNode final
	    : public TArdaNodeDefinition<FArdaPixelStageNode, FArdaPixelStageParameters, EArdaNodeType::Pixel>
	{
	public:
		using FArdaState = FArdaShaderState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.graphics-stage.pixel", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mColor).Resource(P.mVertices).Build();
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			return PrepareShader<FArdaStagePixelShader>(eastl::move(Device));
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelineStages = {State.mStage};
			return Desc;
		}

		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaGraphicsWork&)
		{
			return Context.GetCommands().ClearTexture(*Context.GetTexture(P.mColor), {}, {0.f, 0.f, 0.f, 0.f});
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaPixelStageNode, StagePixels);
	ARDA_DEFINE_GRAPH_NODE(FArdaPixelStageNode, StagePixels);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaFragmentCaptureParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)
		ARDA_NODE_SHADER_BUFFER(mOutput, FArdaFragmentCaptureShader::FArdaParameters, mOutput)
		ARDA_NODE_COLOR_TARGET(mColor)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mCapacity, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaFragmentCaptureNode final
	    : public TArdaNodeDefinition<FArdaFragmentCaptureNode, FArdaFragmentCaptureParameters, EArdaNodeType::Pixel>
	{
	public:
		using FArdaState = FArdaShaderState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.graphics-stage.capture-fragments", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mOutput).Resource(P.mColor).Value(P.mCapacity).Build();
		}

		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters&)
		{
			FArdaDependencyNodeRequirements Result;
			Result.mFeatures.mbRequireFragmentShaderStoresAndAtomics = true;
			return Result;
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			return PrepareShader<FArdaFragmentCaptureShader>(eastl::move(Device));
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelineStages = {State.mStage};
			Desc.BindShader<FArdaFragmentCaptureShader::FArdaParameters>({}, "default", {CapacityValue(P.mCapacity)});
			return Desc;
		}

		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaGraphicsWork&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaFragmentCaptureNode, CaptureStageFragments);
	ARDA_DEFINE_GRAPH_NODE(FArdaFragmentCaptureNode, CaptureStageFragments);

	FArdaRHITextureDesc ColorDescription(uint32_t Width = 8, uint32_t Height = 8)
	{
		FArdaRHITextureDesc Desc;
		Desc.mWidth = Width;
		Desc.mHeight = Height;
		Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		Desc.mUsage = EArdaRHITextureUsage::RenderTarget;
		return Desc;
	}

	FArdaGraphBuffer FArdaRasterStageNode::FetchBuffer(FArdaGraphEdit& Edit,
	    const FArdaParameters& P,
	    const FArdaStageBufferRequest& Request)
	{
		const uint64_t Capacity = Request.mElementCount ? Request.mElementCount : uint64_t(P.mWidth) * P.mHeight;
		if (!Capacity || Capacity > (UINT32_MAX - 16) / sizeof(FArdaVertex))
		{
			Edit.ReportError(
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Fragment capture capacity is invalid."));
			return {};
		}
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16 + Capacity * sizeof(FArdaVertex);
		Desc.mStructureStride = sizeof(uint32_t);
		Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess |
		    EArdaRHIBufferUsage::ShaderResource;
		auto Output = AllocateBuffer(Edit, Desc);
		Clear(Edit, Output);
		auto Color = AllocateTexture(Edit, ColorDescription(P.mWidth, P.mHeight));
		// A dedicated PS materializes the raster inputs. The compiler replays the VS for this draw.
		auto Fragments = StageRasterize(Edit, P);
		CaptureStageFragments(Edit, Fragments, Output, Color, uint32_t(Capacity));
		return Output;
	}

	FArdaGraphBuffer UploadVertices(FArdaGraphEdit& Edit, bool Overlap = false)
	{
		const FArdaVertex Vertices[] = {{{-1.f, -1.f, 0.25f, 1.f}, {1.f, 0.f, 0.f, 1.f}},
		    {{1.f, -1.f, 0.25f, 1.f}, {0.f, 1.f, 0.f, 1.f}},
		    {{0.f, 1.f, 0.25f, 1.f}, {0.f, 0.f, 1.f, 1.f}}};
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = sizeof(Vertices) * (Overlap ? 2 : 1);
		Desc.mStructureStride = sizeof(FArdaVertex);
		Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource;
		auto Buffer = AllocateBuffer(Edit, Desc);
		eastl::vector<uint8_t> Bytes(size_t(Desc.mByteSize));
		std::memcpy(Bytes.data(), Vertices, sizeof(Vertices));
		if (Overlap)
		{
			std::memcpy(Bytes.data() + sizeof(Vertices), Vertices, sizeof(Vertices));
		}
		return Upload(Edit, Buffer, eastl::move(Bytes));
	}

	class FArdaGraphicsStageGpu : public testing::TestWithParam<const char*>
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

	TEST_P(FArdaGraphicsStageGpu, VertexFetchProducesTheSharedShaderTransformAndTwoRasterBranchesDraw)
	{
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		auto Input = UploadVertices(Edit);
		auto Vertices = StageVertices(Edit, Input);
		auto Fetched = FetchBuffer(Edit, Vertices);
		auto Captured = Readback(Edit, Fetched);
		auto First = AllocateTexture(Edit, ColorDescription());
		auto Second = AllocateTexture(Edit, ColorDescription());
		FArdaRasterConfig Raster;
		Raster.mViewport = {0, 8, 0, 8, 0, 1};
		Raster.mRasterState.mCullMode = EArdaRHICullMode::None;
		StagePixels(Edit, Rasterize(Edit, Vertices, Raster), First, Input);
		StagePixels(Edit, Rasterize(Edit, Vertices, Raster.mViewport, Raster.mRasterState), Second, Input);
		auto FirstBytes = Readback(Edit, First);
		auto SecondBytes = Readback(Edit, Second);
		ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_TRUE(Captured);
		ASSERT_EQ(Captured->size(), 3 * sizeof(FArdaVertex));
		FArdaVertex Actual[3];
		std::memcpy(Actual, Captured->data(), sizeof(Actual));
		EXPECT_FLOAT_EQ(Actual[0].mPosition[0], -0.75f);
		EXPECT_FLOAT_EQ(Actual[0].mPosition[1], -0.75f);
		EXPECT_FLOAT_EQ(Actual[1].mPosition[0], 0.75f);
		EXPECT_FLOAT_EQ(Actual[2].mPosition[1], 0.75f);
		for (size_t Index = 0; Index < 3; ++Index)
		{
			EXPECT_FLOAT_EQ(Actual[Index].mPosition[2], 0.25f);
			EXPECT_FLOAT_EQ(Actual[Index].mPosition[3], 1.f);
			EXPECT_FLOAT_EQ(Actual[Index].mColor[Index], 1.f);
		}
		ASSERT_TRUE(FirstBytes);
		ASSERT_TRUE(SecondBytes);
		ASSERT_EQ(FirstBytes->size(), 8u * 8u * 4u);
		EXPECT_EQ(*FirstBytes, *SecondBytes);
		size_t Covered = 0;
		for (size_t Index = 3; Index < FirstBytes->size(); Index += 4)
		{
			Covered += (*FirstBytes)[Index] == 255;
		}
		EXPECT_GT(Covered, 8u);
		EXPECT_LT(Covered, 64u);
	}

	TEST_P(FArdaGraphicsStageGpu, VertexFetchCanMaterializeWithoutRasterOrPixel)
	{
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		auto Vertices = StageVertices(Edit, UploadVertices(Edit));
		auto Captured = Readback(Edit, FetchBuffer(Edit, Vertices));
		ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_TRUE(Captured);
		ASSERT_EQ(Captured->size(), 3 * sizeof(FArdaVertex));
		FArdaVertex Actual[3];
		std::memcpy(Actual, Captured->data(), sizeof(Actual));
		EXPECT_FLOAT_EQ(Actual[0].mPosition[0], -0.75f);
		EXPECT_FLOAT_EQ(Actual[1].mPosition[0], 0.75f);
		EXPECT_FLOAT_EQ(Actual[2].mPosition[1], 0.75f);
	}

	TEST_P(FArdaGraphicsStageGpu, FragmentFetchCapturesInterpolatedRecordsAndReportsBoundedOverflow)
	{
		if (!GetDevice()->GetCapabilities().mbFragmentShaderStoresAndAtomics)
		{
			GTEST_SKIP() << "Pixel shader storage and atomics unavailable";
		}
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		auto Vertices = StageVertices(Edit, UploadVertices(Edit, true), 6u);
		auto Fragments = StageRasterize(Edit, Vertices);
		auto Complete = Readback(Edit, FetchBuffer(Edit, Fragments, {128}));
		auto Limited = Readback(Edit, FetchBuffer(Edit, Fragments, {1}));
		ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_TRUE(Complete);
		ASSERT_TRUE(Limited);
		ASSERT_EQ(Complete->size(), 16 + 128 * sizeof(FArdaVertex));
		ASSERT_EQ(Limited->size(), 16 + sizeof(FArdaVertex));
		uint32_t Header[4], LimitedHeader[4];
		std::memcpy(Header, Complete->data(), sizeof(Header));
		std::memcpy(LimitedHeader, Limited->data(), sizeof(LimitedHeader));
		ASSERT_GT(Header[0], 2u);
		ASSERT_LE(Header[0], 128u);
		EXPECT_EQ(Header[1], 0u);
		EXPECT_EQ(LimitedHeader[0], Header[0]);
		EXPECT_EQ(LimitedHeader[1], Header[0] - 1);
		bool Interpolated = false;
		eastl::vector<uint32_t> PixelCounts(64, 0);
		for (uint32_t Index = 0; Index < Header[0]; ++Index)
		{
			FArdaVertex Record;
			std::memcpy(&Record, Complete->data() + 16 + Index * sizeof(Record), sizeof(Record));
			EXPECT_NEAR(Record.mPosition[2], 0.25f, 0.0001f);
			EXPECT_NEAR(Record.mColor[0] + Record.mColor[1] + Record.mColor[2], 1.f, 0.0001f);
			EXPECT_FLOAT_EQ(Record.mColor[3], 1.f);
			EXPECT_GE(Record.mPosition[0], 0.f);
			EXPECT_GE(Record.mPosition[1], 0.f);
			EXPECT_LT(Record.mPosition[0], 8.f);
			EXPECT_LT(Record.mPosition[1], 8.f);
			if (Record.mPosition[0] >= 0.f && Record.mPosition[0] < 8.f && Record.mPosition[1] >= 0.f &&
			    Record.mPosition[1] < 8.f)
			{
				++PixelCounts[uint32_t(Record.mPosition[1]) * 8 + uint32_t(Record.mPosition[0])];
			}
			Interpolated |= Record.mColor[0] > 0.f && Record.mColor[0] < 1.f;
		}
		EXPECT_TRUE(Interpolated);
		for (auto Count : PixelCounts)
		{
			EXPECT_TRUE(Count == 0 || Count == 2);
		}
	}

	TEST_P(FArdaGraphicsStageGpu, GeometryStageConnectsVertexValuesToRasterization)
	{
		if (!GetDevice()->GetCapabilities().mbGeometryShaders)
		{
			GTEST_SKIP() << "Geometry shaders unavailable";
		}
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		auto Input = UploadVertices(Edit);
		auto Vertices = StageGeometry(Edit, StageVertices(Edit, Input));
		auto Color = AllocateTexture(Edit, ColorDescription());
		StagePixels(Edit, StageRasterize(Edit, Vertices), Color, Input);
		auto Bytes = Readback(Edit, Color);
		ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_TRUE(Bytes);
		ASSERT_EQ(Bytes->size(), 8u * 8u * 4u);
		uint32_t Covered = 0;
		for (size_t Offset = 0; Offset < Bytes->size(); Offset += 4)
		{
			if ((*Bytes)[Offset + 3] == 255)
			{
				++Covered;
				EXPECT_EQ((*Bytes)[Offset], 0);
				EXPECT_EQ((*Bytes)[Offset + 1], 0);
				EXPECT_EQ((*Bytes)[Offset + 2], 255);
			}
		}
		EXPECT_GT(Covered, 8u);
	}

	TEST_P(FArdaGraphicsStageGpu, HullAndDomainStagesPassTypedControlPointsAndShaderOutputs)
	{
		if (!GetDevice()->GetCapabilities().mbTessellationShaders)
		{
			GTEST_SKIP() << "Tessellation shaders unavailable";
		}
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		auto Input = UploadVertices(Edit);
		auto ControlPoints = StageHull(Edit, StageVertices(Edit, Input));
		auto Vertices = StageDomain(Edit, ControlPoints);
		auto Color = AllocateTexture(Edit, ColorDescription());
		StagePixels(Edit, StageRasterize(Edit, Vertices, 8u, 8u, EArdaPipelineStateKind::Graphics, true), Color, Input);
		auto Bytes = Readback(Edit, Color);
		ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_TRUE(Bytes);
		ASSERT_EQ(Bytes->size(), 8u * 8u * 4u);
		uint32_t Covered = 0;
		for (size_t Offset = 0; Offset < Bytes->size(); Offset += 4)
		{
			if ((*Bytes)[Offset + 3] == 255)
			{
				++Covered;
				EXPECT_EQ((*Bytes)[Offset], 0);
				EXPECT_EQ((*Bytes)[Offset + 1], 255);
				EXPECT_EQ((*Bytes)[Offset + 2], 255);
			}
		}
		EXPECT_GT(Covered, 8u);
	}

	void VerifyMeshStages(bool WithAmplification)
	{
		const auto Tier = WithAmplification ? EArdaRHIMeshShaderTier::MeshAndAmplificationShaders
		                                    : EArdaRHIMeshShaderTier::MeshShadersOnly;
		if (!GetDevice()->GetCapabilities().SupportsMeshShaderTier(Tier))
		{
			GTEST_SKIP() << (WithAmplification ? "Amplification shaders unavailable" : "Mesh shaders unavailable");
		}
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		auto Input = UploadVertices(Edit);
		FArdaGraphVertices Vertices;
		if (WithAmplification)
		{
			auto Payload = StageAmplify(Edit, Input);
			Vertices = StagePayloadMesh(Edit, Payload, Input);
		}
		else
		{
			Vertices = StageMesh(Edit, FArdaGraphMeshPayload{}, Input);
		}
		auto First = AllocateTexture(Edit, ColorDescription());
		auto Second = AllocateTexture(Edit, ColorDescription());
		StagePixels(Edit, StageRasterize(Edit, Vertices, 8u, 8u, EArdaPipelineStateKind::Meshlet), First, Input);
		StagePixels(Edit, StageRasterize(Edit, Vertices, 8u, 8u, EArdaPipelineStateKind::Meshlet), Second, Input);
		auto FirstBytes = Readback(Edit, First);
		auto SecondBytes = Readback(Edit, Second);
		ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_TRUE(FirstBytes);
		ASSERT_TRUE(SecondBytes);
		ASSERT_EQ(FirstBytes->size(), 8u * 8u * 4u);
		EXPECT_EQ(*FirstBytes, *SecondBytes);
		uint32_t Covered = 0;
		for (size_t Offset = 0; Offset < FirstBytes->size(); Offset += 4)
		{
			if ((*FirstBytes)[Offset + 3] == 255)
			{
				++Covered;
				const int Sum = (*FirstBytes)[Offset] + (*FirstBytes)[Offset + 1] + (*FirstBytes)[Offset + 2];
				EXPECT_NEAR(Sum, 255, 2);
			}
		}
		EXPECT_GT(Covered, 8u);
		EXPECT_LT(Covered, 64u);
	}

	TEST_P(FArdaGraphicsStageGpu, MeshStageAssemblesIndependentRasterBranchesWithoutAmplification)
	{
		VerifyMeshStages(false);
	}

	TEST_P(FArdaGraphicsStageGpu, AmplificationPayloadConnectsMeshStageWithResourceBindingsAcrossStages)
	{
		VerifyMeshStages(true);
	}

	class FArdaAutomaticInstanceVertexShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaAutomaticInstanceVertexShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaAutomaticInstanceVertexShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaAutomaticInstancing.hlsl",
	    "AutomaticInstanceVertex",
	    "InstancingVS",
	    EArdaRHIShaderStage::Vertex)

	class FArdaAutomaticInstancePixelShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_PUSH_CONSTANTS(uint32_t, mTint, 0, 0, EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaAutomaticInstancePixelShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaAutomaticInstancePixelShader,
	    ARDA_RDG_TEST_SHADER_SOURCE_DIR "/ArdaAutomaticInstancing.hlsl",
	    "AutomaticInstancePixel",
	    "InstancingPS",
	    EArdaRHIShaderStage::Pixel)

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaAutomaticInstanceVertexParameters)
		ARDA_NODE_BUFFER(mInstances, EArdaDependencyAccess::Read, EArdaRHIResourceState::VertexBuffer)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mStartInstance, 0)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mInstanceCount, 1)
		ARDA_NODE_PARAMETER(FArdaGraphBuffer, mIndices)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaAutomaticInstanceVertexNode final : public TArdaNodeDefinition<FArdaAutomaticInstanceVertexNode,
	                                                   FArdaAutomaticInstanceVertexParameters,
	                                                   EArdaNodeType::Vertex>
	{
	public:
		using FArdaState = FArdaShaderState;
		static constexpr EArdaGraphicsInstancing mGraphicsInstancing = EArdaGraphicsInstancing::ContiguousInstances;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.automatic-instancing.vertex", 1};
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			FArdaRHIVertexAttributeDesc Attribute;
			Attribute.mSemanticName = "INSTANCE";
			Attribute.mFormat = EArdaRHIFormat::RGBA32Float;
			Attribute.mElementStride = 4 * sizeof(float);
			Attribute.mbInstanced = true;
			auto Layout = Device->CreateInputLayout({Attribute});
			if (!Layout)
			{
				return {{}, Layout.mStatus};
			}
			return PrepareShader<FArdaAutomaticInstanceVertexShader>(eastl::move(Device), eastl::move(Layout.mValue));
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelineStages = {State.mStage};
			Desc.mGraphicsInputLayout = State.mInputLayout;
			if (Parameters.mIndices)
			{
				Desc.mAccesses.push_back(
				    {Parameters.mIndices.GetHandle(), EArdaDependencyAccess::Read, EArdaRHIResourceState::IndexBuffer});
			}
			return Desc;
		}

		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaGraphicsWork& Work)
		{
			Work.mDraw.mVertexCount = 3;
			Work.mDraw.mStartInstance = Parameters.mStartInstance;
			Work.mDraw.mInstanceCount = Parameters.mInstanceCount;
			Work.mState.mVertexBuffers = {{Context.GetBuffer(Parameters.mInstances), 0, 0}};
			Work.mbIndexed = bool(Parameters.mIndices);
			if (Work.mbIndexed)
			{
				Work.mState.mIndexBuffer = Context.GetBuffer(Parameters.mIndices);
				Work.mState.mIndexFormat = EArdaRHIFormat::R32UInt;
			}
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaAutomaticInstanceVertexNode, AutomaticInstanceVerticesTest);
	ARDA_DEFINE_GRAPH_NODE(FArdaAutomaticInstanceVertexNode, AutomaticInstanceVerticesTest);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaAutomaticInstancePixelParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)
		ARDA_NODE_COLOR_TARGET_ACCESS(mColor, EArdaDependencyAccess::ReadWrite)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mTint, 255)
	ARDA_END_NODE_PARAMETER_STRUCT()

	template <EArdaGraphicsInstancing Instancing>
	class TArdaAutomaticInstancePixelNode final
	    : public TArdaNodeDefinition<TArdaAutomaticInstancePixelNode<Instancing>,
	          FArdaAutomaticInstancePixelParameters,
	          EArdaNodeType::Pixel>
	{
	public:
		using FArdaParameters = FArdaAutomaticInstancePixelParameters;
		using FArdaState = FArdaShaderState;
		static constexpr EArdaGraphicsInstancing mGraphicsInstancing = Instancing;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {Instancing == EArdaGraphicsInstancing::Disabled ? "test.automatic-instancing.reference-pixel"
			                                                        : "test.automatic-instancing.pixel",
			    1};
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			return PrepareShader<FArdaAutomaticInstancePixelShader>(eastl::move(Device));
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelineStages = {State.mStage};
			FArdaDependencyShaderValue Value;
			Value.mMember = "mTint";
			Value.mBytes.resize(sizeof(uint32_t));
			std::memcpy(Value.mBytes.data(), &Parameters.mTint, sizeof(uint32_t));
			Desc.BindShader<FArdaAutomaticInstancePixelShader::FArdaParameters>({}, "default", {Value});
			return Desc;
		}
	};

	using FArdaAutomaticInstancePixelNode =
	    TArdaAutomaticInstancePixelNode<EArdaGraphicsInstancing::ContiguousInstances>;
	using FArdaUnmergedInstancePixelNode = TArdaAutomaticInstancePixelNode<EArdaGraphicsInstancing::Disabled>;
	ARDA_DECLARE_GRAPH_NODE(FArdaAutomaticInstancePixelNode, AutomaticInstancePixelsTest);
	ARDA_DEFINE_GRAPH_NODE(FArdaAutomaticInstancePixelNode, AutomaticInstancePixelsTest);
	ARDA_DECLARE_GRAPH_NODE(FArdaUnmergedInstancePixelNode, UnmergedInstancePixelsTest);
	ARDA_DEFINE_GRAPH_NODE(FArdaUnmergedInstancePixelNode, UnmergedInstancePixelsTest);

	void VerifyAutomaticInstancing(bool bIndexed, bool bResetInstanceRange)
	{
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		// Isolate batching/recording from the independent scheduler's memory-lifetime choices.
		Options.mRules.mCompileRules.erase(eastl::remove_if(Options.mRules.mCompileRules.begin(),
		                                       Options.mRules.mCompileRules.end(),
		                                       [](const auto& Rule)
		                                       {
			                                       return dynamic_cast<const FArdaSearchOrderRule*>(Rule.get()) !=
			                                           nullptr;
		                                       }),
		    Options.mRules.mCompileRules.end());
		ASSERT_TRUE(Edit.SetOptions(Options));
		const float Instances[8] = {-0.5f, 0.f, 0.f, 0.f, 0.5f, 0.f, 1.f, 0.f};
		FArdaRHIBufferDesc InstanceDesc;
		InstanceDesc.mByteSize = sizeof(Instances);
		InstanceDesc.mStructureStride = 4 * sizeof(float);
		eastl::vector<uint8_t> InstanceBytes(sizeof(Instances));
		std::memcpy(InstanceBytes.data(), Instances, sizeof(Instances));
		const auto InstanceBuffer = Upload(Edit, AllocateBuffer(Edit, InstanceDesc), eastl::move(InstanceBytes));
		FArdaGraphBuffer IndexBuffer;
		if (bIndexed)
		{
			const uint32_t Indices[3] = {0, 1, 2};
			FArdaRHIBufferDesc IndexDesc;
			IndexDesc.mByteSize = sizeof(Indices);
			eastl::vector<uint8_t> IndexBytes(sizeof(Indices));
			std::memcpy(IndexBytes.data(), Indices, sizeof(Indices));
			IndexBuffer = Upload(Edit, AllocateBuffer(Edit, IndexDesc), eastl::move(IndexBytes));
		}
		const auto Reference = Clear(Edit, AllocateTexture(Edit, ColorDescription(32, 16)));
		const auto Automatic = Clear(Edit, AllocateTexture(Edit, ColorDescription(32, 16)));
		FArdaRasterConfig Raster;
		Raster.mViewport = {0.f, 32.f, 0.f, 16.f, 0.f, 1.f};
		Raster.mRasterState.mCullMode = EArdaRHICullMode::None;
		// Compare against the original separate draws, including their base-instance fetch semantics.
		UnmergedInstancePixelsTest(Edit,
		    Rasterize(Edit, AutomaticInstanceVerticesTest(Edit, InstanceBuffer, 0u, 1u, IndexBuffer), Raster),
		    Reference);
		UnmergedInstancePixelsTest(Edit,
		    Rasterize(Edit,
		        AutomaticInstanceVerticesTest(Edit, InstanceBuffer, bResetInstanceRange ? 0u : 1u, 1u, IndexBuffer),
		        Raster),
		    Reference);
		const auto First = AutomaticInstancePixelsTest(Edit,
		    Rasterize(Edit, AutomaticInstanceVerticesTest(Edit, InstanceBuffer, 0u, 1u, IndexBuffer), Raster),
		    Automatic);
		const auto Second = AutomaticInstancePixelsTest(Edit,
		    Rasterize(Edit,
		        AutomaticInstanceVerticesTest(Edit, InstanceBuffer, bResetInstanceRange ? 0u : 1u, 1u, IndexBuffer),
		        Raster),
		    Automatic);
		const auto ReferenceBytes = Readback(Edit, Reference);
		const auto AutomaticBytes = Readback(Edit, Automatic);
		ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& Candidates = Graph.GetCompileResult().mInstancedDraws;
		ASSERT_EQ(Candidates.size(), 1u);
		ASSERT_EQ(Candidates.front().size(), 2u);
		EXPECT_EQ(Candidates.front()[0], First.GetProducer());
		EXPECT_EQ(Candidates.front()[1], Second.GetProducer());
		const auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		EXPECT_EQ(Executed.mRecordedGraphicsDrawCount, bResetInstanceRange ? 4u : 3u);
		EXPECT_EQ(Executed.mMergedGraphicsDrawCount, bResetInstanceRange ? 0u : 1u);
		ASSERT_TRUE(ReferenceBytes);
		ASSERT_TRUE(AutomaticBytes);
		EXPECT_EQ(*ReferenceBytes, *AutomaticBytes);
		uint32_t RedPixels = 0;
		uint32_t GreenPixels = 0;
		for (size_t Index = 0; Index < AutomaticBytes->size(); Index += 4)
		{
			RedPixels += (*AutomaticBytes)[Index] == 255;
			GreenPixels += (*AutomaticBytes)[Index + 1] == 255;
		}
		EXPECT_GT(RedPixels, 12u);
		if (bResetInstanceRange)
		{
			EXPECT_EQ(GreenPixels, 0u);
		}
		else
		{
			EXPECT_GT(GreenPixels, 12u);
		}
	}

	TEST_P(FArdaGraphicsStageGpu, AutomaticInstancingPreservesIndexedInstanceRangesAndImage)
	{
		VerifyAutomaticInstancing(true, false);
	}

	TEST_P(FArdaGraphicsStageGpu, AutomaticInstancingPreservesNonindexedInstanceRangesAndImage)
	{
		VerifyAutomaticInstancing(false, false);
	}

	TEST_P(FArdaGraphicsStageGpu, AutomaticInstancingFallsBackWhenInstanceRangesReset)
	{
		VerifyAutomaticInstancing(false, true);
	}

	TEST_P(FArdaGraphicsStageGpu, PipelineBatchRecordsDifferentComputeBindingsAndDispatchArguments)
	{
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		Options.mRules.mCompileRules.erase(eastl::remove_if(Options.mRules.mCompileRules.begin(),
		                                       Options.mRules.mCompileRules.end(),
		                                       [](const auto& Rule)
		                                       {
			                                       return dynamic_cast<const FArdaSearchOrderRule*>(Rule.get()) !=
			                                           nullptr;
		                                       }),
		    Options.mRules.mCompileRules.end());
		ASSERT_TRUE(Edit.SetOptions(Options));
		const auto Input = UploadVertices(Edit);
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 3 * sizeof(FArdaVertex);
		Desc.mStructureStride = sizeof(FArdaVertex);
		const auto FirstOutput = AllocateBuffer(Edit, Desc);
		Desc.mByteSize = sizeof(FArdaVertex);
		const auto SecondOutput = AllocateBuffer(Edit, Desc);
		const auto First = CaptureStageVertices(Edit, Input, FirstOutput, 3u);
		const auto Second = CaptureStageVertices(Edit, Input, SecondOutput, 1u);
		const auto FirstBytes = Readback(Edit, FirstOutput);
		const auto SecondBytes = Readback(Edit, SecondOutput);
		ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		bool bFoundBatch = false;
		for (const auto& Dispatch : Graph.GetCompileResult().mDispatches)
		{
			bFoundBatch |=
			    Dispatch.mNodes == eastl::vector<FArdaGraphNodeHandle>{First.GetHandle(), Second.GetHandle()};
		}
		EXPECT_TRUE(bFoundBatch);
		const auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_EQ(FirstBytes->size(), 3 * sizeof(FArdaVertex));
		ASSERT_EQ(SecondBytes->size(), sizeof(FArdaVertex));
		EXPECT_EQ(std::memcmp(FirstBytes->data(), SecondBytes->data(), sizeof(FArdaVertex)), 0);
		FArdaVertex Value;
		std::memcpy(&Value, SecondBytes->data(), sizeof(Value));
		EXPECT_FLOAT_EQ(Value.mPosition[0], -0.75f);
		EXPECT_FLOAT_EQ(Value.mPosition[1], -0.75f);
	}

	TEST_P(FArdaGraphicsStageGpu, ReusedTransientStorageKeepsPipelineBatchBoundary)
	{
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Memory;
		Options.mRules.mCompileRules.erase(eastl::remove_if(Options.mRules.mCompileRules.begin(),
		                                       Options.mRules.mCompileRules.end(),
		                                       [](const auto& Rule)
		                                       {
			                                       return dynamic_cast<const FArdaSearchOrderRule*>(Rule.get()) !=
			                                           nullptr;
		                                       }),
		    Options.mRules.mCompileRules.end());
		ASSERT_TRUE(Edit.SetOptions(Options));
		const auto Input = UploadVertices(Edit);
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 3 * sizeof(FArdaVertex);
		Desc.mStructureStride = sizeof(FArdaVertex);
		const auto FirstOutput = AllocateBuffer(Edit, Desc);
		const auto SecondOutput = AllocateBuffer(Edit, Desc);
		const auto First = CaptureStageVertices(Edit, Input, FirstOutput, 3u, true);
		const auto Second = CaptureStageVertices(Edit, Input, SecondOutput, 3u, true);
		const auto Bytes = Readback(Edit, SecondOutput);
		ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& Plan = *Graph.GetCompileResult().mMemoryPlan;
		bool bReusedStorage = false;
		for (const auto& Activation : Plan.mAliasActivations)
		{
			bReusedStorage |= Activation.mResource == SecondOutput.GetHandle().mIndex &&
			    Activation.mConsumer == Second.GetHandle().GetIndex() &&
			    eastl::find(Activation.mPreviousResources.begin(),
			        Activation.mPreviousResources.end(),
			        FirstOutput.GetHandle().mIndex) != Activation.mPreviousResources.end();
		}
		ASSERT_TRUE(bReusedStorage);
		for (const auto& Dispatch : Graph.GetCompileResult().mDispatches)
		{
			const bool bFirst =
			    eastl::find(Dispatch.mNodes.begin(), Dispatch.mNodes.end(), First.GetHandle()) != Dispatch.mNodes.end();
			const bool bSecond = eastl::find(Dispatch.mNodes.begin(), Dispatch.mNodes.end(), Second.GetHandle()) !=
			    Dispatch.mNodes.end();
			EXPECT_FALSE(bFirst && bSecond);
		}
		const auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_EQ(Bytes->size(), 3 * sizeof(FArdaVertex));
		FArdaVertex Value;
		std::memcpy(&Value, Bytes->data(), sizeof(Value));
		EXPECT_FLOAT_EQ(Value.mPosition[0], -0.75f);
		EXPECT_FLOAT_EQ(Value.mPosition[1], -0.75f);
	}

	TEST_P(FArdaGraphicsStageGpu, DifferentPixelConstantsBatchWithoutChangingDrawSemantics)
	{
		FArdaDependencyGraph Graph(GetDevice());
		FArdaGraphEdit Edit(Graph);
		const float Instances[8] = {-0.5f, 0.f, 0.f, 0.f, 0.5f, 0.f, 1.f, 0.f};
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = sizeof(Instances);
		Desc.mStructureStride = 4 * sizeof(float);
		eastl::vector<uint8_t> Bytes(sizeof(Instances));
		std::memcpy(Bytes.data(), Instances, sizeof(Instances));
		const auto InstanceBuffer = Upload(Edit, AllocateBuffer(Edit, Desc), eastl::move(Bytes));
		const auto Target = Clear(Edit, AllocateTexture(Edit, ColorDescription(32, 16)));
		FArdaRasterConfig Raster;
		Raster.mViewport = {0.f, 32.f, 0.f, 16.f, 0.f, 1.f};
		Raster.mRasterState.mCullMode = EArdaRHICullMode::None;
		const auto First = AutomaticInstancePixelsTest(Edit,
		    Rasterize(Edit, AutomaticInstanceVerticesTest(Edit, InstanceBuffer, 0u), Raster),
		    Target,
		    255u);
		const auto Second = AutomaticInstancePixelsTest(Edit,
		    Rasterize(Edit, AutomaticInstanceVerticesTest(Edit, InstanceBuffer, 1u), Raster),
		    Target,
		    127u);
		const auto ReadbackBytes = Readback(Edit, Target);
		ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		bool bGrouped = false;
		for (const auto& Batch : Graph.GetCompileResult().mPipelineBatches)
		{
			bGrouped |= Batch == eastl::vector<FArdaGraphNodeHandle>{First.GetProducer(), Second.GetProducer()};
		}
		EXPECT_TRUE(bGrouped);
		EXPECT_TRUE(Graph.GetCompileResult().mInstancedDraws.empty());
		const auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		EXPECT_EQ(Executed.mRecordedGraphicsDrawCount, 2u);
		EXPECT_EQ(Executed.mMergedGraphicsDrawCount, 0u);
		ASSERT_EQ(ReadbackBytes->size(), 32u * 16u * 4u);
		uint32_t RedPixels = 0;
		uint32_t GreenPixels = 0;
		std::map<uint32_t, uint32_t> Histogram;
		for (size_t Index = 0; Index < ReadbackBytes->size(); Index += 4)
		{
			RedPixels += (*ReadbackBytes)[Index] == 255;
			GreenPixels += (*ReadbackBytes)[Index + 1] == 127;
			uint32_t Color;
			std::memcpy(&Color, ReadbackBytes->data() + Index, sizeof(Color));
			++Histogram[Color];
		}
		std::ostringstream Diagnostic;
		Diagnostic << "RGBA byte histogram:";
		for (const auto& Entry : Histogram)
		{
			Diagnostic << " 0x" << std::hex << Entry.first << std::dec << ":" << Entry.second;
		}
		Diagnostic << "; execution:";
		for (const auto Node : Graph.GetCompileResult().mExecutionOrder)
		{
			Diagnostic << " " << Node.GetIndex();
		}
		Diagnostic << "; dispatches:";
		for (const auto& Dispatch : Graph.GetCompileResult().mDispatches)
		{
			Diagnostic << " [q" << uint32_t(Dispatch.mQueue) << ":";
			for (const auto Node : Dispatch.mNodes)
			{
				Diagnostic << " " << Node.GetIndex();
			}
			Diagnostic << "]";
		}
		EXPECT_GT(RedPixels, 12u) << Diagnostic.str();
		EXPECT_GT(GreenPixels, 12u) << Diagnostic.str();
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaGraphicsStageGpu, testing::Values("native-d3d12", "native-vulkan"));
}
