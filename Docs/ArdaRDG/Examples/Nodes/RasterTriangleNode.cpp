#include "RasterTriangleNode.h"
#include "RecipeNodeSupport.h"
#include <cstring>

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaRecipeTriangleVertexNode, RecipeTriangleVS);
	ARDA_DEFINE_GRAPH_NODE(FArdaRecipeTriangleRasterNode, RecipeTriangleRasterize);
	ARDA_DEFINE_GRAPH_NODE(FArdaRecipeTrianglePixelNode, RecipeTrianglePS);
	ARDA_DEFINE_GRAPH_NODE(FArdaRecipeVertexCaptureNode, CaptureRecipeVertices);

	class FArdaRecipeRasterVertexShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaRecipeRasterVertexShader);
	};

	class FArdaRecipeRasterPixelShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaRecipeRasterPixelShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaRecipeRasterVertexShader,
	    ARDA_RDG_RECIPE_SHADER_DIR "/RasterTriangle.hlsl",
	    "RecipeVertex",
	    "RecipeVS",
	    EArdaRHIShaderStage::Vertex)
	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaRecipeRasterPixelShader,
	    ARDA_RDG_RECIPE_SHADER_DIR "/RasterTriangle.hlsl",
	    "RecipeRasterPixel",
	    "RecipeRasterPS",
	    EArdaRHIShaderStage::Pixel)

	class FArdaRecipeVertexCaptureShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_UAV(mOutput, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_PUSH_CONSTANTS(uint32_t, mVertexCount, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaRecipeVertexCaptureShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaRecipeVertexCaptureShader,
	    ARDA_RDG_RECIPE_SHADER_DIR "/RasterTriangle.hlsl",
	    "RecipeVertexCapture",
	    "RecipeCaptureVerticesCS",
	    EArdaRHIShaderStage::Compute)

	struct FArdaRecipeTriangleVertexNode::FArdaState
	{
		FArdaGlobalShaderMap mShaders;
		FArdaInductorPipelineContribution mStage;
	};

	FArdaDependencyNodeMetadata FArdaRecipeTriangleVertexNode::GetMetadata()
	{
		return {"recipe.triangle.vertex", 1};
	}

	FArdaRHIStatus FArdaRecipeTriangleVertexNode::Validate(const FArdaParameters& P)
	{
		if (!P.mVertexCount || P.mVertexCount > 3)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "The recipe provides one to three vertices.");
		}
		return {};
	}

	FArdaGraphBuffer FArdaRecipeTriangleVertexNode::FetchBuffer(FArdaGraphEdit& Edit,
	    const FArdaParameters& P,
	    const FArdaStageBufferRequest& Request)
	{
		const uint64_t Count = Request.mElementCount ? Request.mElementCount : P.mVertexCount;
		if (!Count || Count > P.mVertexCount)
		{
			Edit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Recipe vertex capture must fit the producer's vertex count."));
			return {};
		}
		return CaptureRecipeVertices(Edit, {{}, uint32_t(Count)}).GetBuffer("Positions");
	}

	struct FArdaRecipeVertexCaptureNode::FArdaState
	{
		FArdaGlobalShaderMap mShaders;
		FArdaInductorPipelineContribution mStage;
	};

	FArdaDependencyNodeMetadata FArdaRecipeVertexCaptureNode::GetMetadata()
	{
		return {"recipe.triangle.capture-vertices", 1};
	}

	eastl::string FArdaRecipeVertexCaptureNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder().Resource(P.mOutput).Value(P.mVertexCount).Build();
	}

	FArdaRHIStatus FArdaRecipeVertexCaptureNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHIBufferDesc Desc;
		Desc.mStructureStride = sizeof(float) * 4;
		Desc.mByteSize = Desc.mStructureStride * P.mVertexCount;
		Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource |
		    EArdaRHIBufferUsage::UnorderedAccess;
		return C.Buffer(P.mOutput, "Positions", Desc);
	}

	FArdaRHIStatus FArdaRecipeVertexCaptureNode::Validate(const FArdaParameters& P)
	{
		return FArdaRecipeTriangleVertexNode::Validate({P.mVertexCount});
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaRecipeVertexCaptureNode::FArdaState>> FArdaRecipeVertexCaptureNode::
	    Prepare(FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Vertex capture requires a device.")};
		}
		auto State = eastl::make_shared<FArdaState>();
		if (!State->mShaders.Initialize(Device))
		{
			return {{}, ShaderError(State->mShaders)};
		}
		const auto* Shader = State->mShaders.Find(FArdaRecipeVertexCaptureShader::GetStaticType());
		if (!Shader)
		{
			return {{}, ShaderError(State->mShaders)};
		}
		State->mStage.mShader = Shader->GetShader();
		State->mStage.mBindingLayouts = Shader->GetBindingLayouts();
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaRecipeVertexCaptureNode::Describe(const FArdaParameters& P, const FArdaState& State)
	{
		FArdaDependencyShaderValue Count;
		Count.mMember = "mVertexCount";
		Count.mBytes.resize(sizeof(P.mVertexCount));
		std::memcpy(Count.mBytes.data(), &P.mVertexCount, sizeof(P.mVertexCount));
		FArdaDependencyNodeDesc Desc;
		Desc.BindShader<FArdaRecipeVertexCaptureShader::FArdaParameters>({{"mOutput", P.mOutput}}, "default", {Count});
		Desc.mPipelineStages = {State.mStage};
		Desc.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute}};
		return Desc;
	}

	FArdaRHIStatus FArdaRecipeVertexCaptureNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters&,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		if (auto Status = C.SetComputeState(); !Status)
		{
			return Status;
		}
		C.GetCommands().Dispatch(1, 1, 1);
		return {};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaRecipeTriangleVertexNode::FArdaState>> FArdaRecipeTriangleVertexNode::
	    Prepare(FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Vertex preparation needs a device.")};
		}
		auto State = eastl::make_shared<FArdaState>();
		if (!State->mShaders.Initialize(Device))
		{
			return {{}, ShaderError(State->mShaders)};
		}
		const auto* Shader = State->mShaders.Find(FArdaRecipeRasterVertexShader::GetStaticType());
		if (!Shader)
		{
			return {{}, ShaderError(State->mShaders)};
		}
		State->mStage.mShader = Shader->GetShader();
		State->mStage.mBindingLayouts = Shader->GetBindingLayouts();
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaRecipeTriangleVertexNode::Describe(const FArdaParameters&, const FArdaState& State)
	{
		FArdaDependencyNodeDesc Desc;
		Desc.mPipelineStages = {State.mStage};
		return Desc;
	}

	FArdaRHIStatus FArdaRecipeTriangleVertexNode::ConfigureGraphics(FArdaDependencyExecutionContext&,
	    const FArdaParameters& P,
	    const FArdaState&,
	    FArdaInstanceState&,
	    FArdaGraphicsWork& Work)
	{
		Work.mDraw.mVertexCount = P.mVertexCount;
		return {};
	}

	struct FArdaRecipeTriangleRasterNode::FArdaState
	{
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
	};

	FArdaDependencyNodeMetadata FArdaRecipeTriangleRasterNode::GetMetadata()
	{
		return {"recipe.triangle.rasterize", 1};
	}

	FArdaRHIStatus FArdaRecipeTriangleRasterNode::Validate(const FArdaParameters& P)
	{
		if (!P.mWidth || !P.mHeight)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Raster target extent must be nonzero.");
		}
		return {};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaRecipeTriangleRasterNode::FArdaState>> FArdaRecipeTriangleRasterNode::
	    Prepare(FArdaRHIDeviceRef)
	{
		auto State = eastl::make_shared<FArdaState>();
		auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		Config->mKind = EArdaPipelineStateKind::Graphics;
		auto& Desc = Config->mGraphics.mDesc;
		Desc.mRasterState.mCullMode = EArdaRHICullMode::None;
		Desc.mDepthStencilState.mbDepthTest = false;
		Desc.mDepthStencilState.mbDepthWrite = false;
		Desc.mSampleCount = 0;
		State->mConfiguration = eastl::move(Config);
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaRecipeTriangleRasterNode::Describe(const FArdaParameters&, const FArdaState& State)
	{
		FArdaDependencyNodeDesc Desc;
		FArdaInductorPipelineContribution Raster;
		Raster.mConfiguration = State.mConfiguration;
		Desc.mPipelineStages = {eastl::move(Raster)};
		return Desc;
	}

	FArdaRHIStatus FArdaRecipeTriangleRasterNode::ConfigureGraphics(FArdaDependencyExecutionContext&,
	    const FArdaParameters& P,
	    const FArdaState&,
	    FArdaInstanceState&,
	    FArdaGraphicsWork& Work)
	{
		Work.mState.mViewports = {{0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f}};
		Work.mState.mScissors = {{0, int32_t(P.mWidth), 0, int32_t(P.mHeight)}};
		return {};
	}

	struct FArdaRecipeTrianglePixelNode::FArdaState
	{
		FArdaGlobalShaderMap mShaders;
		FArdaInductorPipelineContribution mStage;
	};

	FArdaDependencyNodeMetadata FArdaRecipeTrianglePixelNode::GetMetadata()
	{
		return {"recipe.triangle.pixel", 1};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaRecipeTrianglePixelNode::FArdaState>> FArdaRecipeTrianglePixelNode::
	    Prepare(FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Pixel preparation needs a device.")};
		}
		auto State = eastl::make_shared<FArdaState>();
		if (!State->mShaders.Initialize(Device))
		{
			return {{}, ShaderError(State->mShaders)};
		}
		const auto* Shader = State->mShaders.Find(FArdaRecipeRasterPixelShader::GetStaticType());
		if (!Shader)
		{
			return {{}, ShaderError(State->mShaders)};
		}
		State->mStage.mShader = Shader->GetShader();
		State->mStage.mBindingLayouts = Shader->GetBindingLayouts();
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaRecipeTrianglePixelNode::Describe(const FArdaParameters&, const FArdaState& State)
	{
		FArdaDependencyNodeDesc Desc;
		Desc.mPipelineStages = {State.mStage};
		return Desc;
	}

	FArdaRHIStatus FArdaRecipeTrianglePixelNode::ConfigureGraphics(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState&,
	    FArdaInstanceState&,
	    FArdaGraphicsWork&)
	{
		return C.GetCommands().ClearTexture(*C.GetTexture(P.mColor), {}, {0.f, 0.f, 0.f, 1.f});
	}
}
