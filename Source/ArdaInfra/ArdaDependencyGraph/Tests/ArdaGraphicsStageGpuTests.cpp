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
	};

	template <class Shader>
	TArdaRHIResult<eastl::shared_ptr<const FArdaShaderState>> PrepareShader(FArdaRHIDeviceRef Device)
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

	struct FArdaVertexCaptureParameters
	{
		FArdaGraphBuffer mInput, mOutput;
		uint32_t mCount = 3;
	};

	class FArdaVertexCaptureNode final
	    : public TArdaComputeDependencyNode<FArdaVertexCaptureNode, FArdaVertexCaptureParameters>
	{
	public:
		using FArdaState = FArdaShaderState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.graphics-stage.capture-vertices", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mInput).Resource(P.mOutput).Value(P.mCount).Build();
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
		{
			return PrepareShader<FArdaVertexCaptureShader>(eastl::move(Device));
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.BindShader<FArdaVertexCaptureShader::FArdaParameters>({{"mInput", P.mInput}, {"mOutput", P.mOutput}},
			    "default",
			    {CapacityValue(P.mCount)});
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

	struct FArdaVertexStageParameters
	{
		FArdaGraphBuffer mVertices;
		uint32_t mCount = 3;
	};

	class FArdaVertexStageNode final : public TArdaVertexStageNode<FArdaVertexStageNode, FArdaVertexStageParameters>
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
			Desc.BindShader<FArdaStageVertexShader::FArdaParameters>({{"mInput", P.mVertices}});
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

	struct FArdaGeometryStageParameters
	{
		FArdaGraphVertices mInput;
	};

	struct FArdaHullStageParameters
	{
		FArdaGraphVertices mInput;
	};

	struct FArdaDomainStageParameters
	{
		FArdaGraphControlPoints mInput;
	};

	struct FArdaAmplificationStageParameters
	{
		FArdaGraphBuffer mVertices;
	};

	struct FArdaMeshStageParameters
	{
		FArdaGraphMeshPayload mInput;
		FArdaGraphBuffer mVertices;
	};

	struct FArdaPayloadMeshStageParameters
	{
		FArdaGraphMeshPayload mInput;
		FArdaGraphBuffer mVertices;
	};

	template <class Shader, class Parameters, EArdaGraphicsStage Stage>
	class TArdaAdditionalShaderNode final
	    : public TArdaGraphicsStageNode<TArdaAdditionalShaderNode<Shader, Parameters, Stage>, Parameters, Stage>
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
			if constexpr (Stage == EArdaGraphicsStage::Amplification || Stage == EArdaGraphicsStage::Mesh)
			{
				Desc.BindShader<typename Shader::FArdaParameters>({{"mInput", P.mVertices}});
			}
			return Desc;
		}
	};

	using TArdaGeometryStageNode =
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
	ARDA_DECLARE_GRAPH_NODE(TArdaGeometryStageNode, StageGeometry);
	ARDA_DEFINE_GRAPH_NODE(TArdaGeometryStageNode, StageGeometry);
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

	struct FArdaRasterStageParameters
	{
		FArdaGraphVertices mInput;
		uint32_t mWidth = 8, mHeight = 8;
		EArdaPipelineStateKind mFamily = EArdaPipelineStateKind::Graphics;
		bool mbTessellation = false;
	};

	class FArdaRasterStageNode final : public TArdaRasterStageNode<FArdaRasterStageNode, FArdaRasterStageParameters>
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

	struct FArdaPixelStageParameters
	{
		FArdaGraphFragments mInput;
		FArdaGraphTexture mColor;
		FArdaGraphBuffer mVertices;
	};

	class FArdaPixelStageNode final : public TArdaPixelStageNode<FArdaPixelStageNode, FArdaPixelStageParameters>
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
			Desc.mAccesses = {{P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
			Desc.mColorTargets = {P.mColor};
			Desc.BindShader<FArdaStagePixelShader::FArdaParameters>({{"mInput", P.mVertices}});
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

	struct FArdaFragmentCaptureParameters
	{
		FArdaGraphFragments mInput;
		FArdaGraphBuffer mOutput;
		FArdaGraphTexture mColor;
		uint32_t mCapacity = 0;
	};

	class FArdaFragmentCaptureNode final
	    : public TArdaPixelStageNode<FArdaFragmentCaptureNode, FArdaFragmentCaptureParameters>
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
			Desc.BindShader<FArdaFragmentCaptureShader::FArdaParameters>(
			    {{"mOutput", P.mOutput, 0, EArdaDependencyAccess::ReadWrite}},
			    "default",
			    {CapacityValue(P.mCapacity)});
			Desc.mAccesses.push_back({P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget});
			Desc.mColorTargets = {P.mColor};
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
		auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto Executed = Graph.Execute();
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
		auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto Executed = Graph.Execute();
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
		auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto Executed = Graph.Execute();
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
		auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		auto Executed = Graph.Execute();
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
		auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		auto Executed = Graph.Execute();
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
		auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		auto Executed = Graph.Execute();
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

	INSTANTIATE_TEST_SUITE_P(Native, FArdaGraphicsStageGpu, testing::Values("native-d3d12", "native-vulkan"));
}
