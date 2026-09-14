#include "RasterTriangleNode.h"
#include "RecipeNodeSupport.h"

namespace arda
{
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

	struct FArdaRasterTriangleNode::FArdaState
	{
		FArdaGlobalShaderMap mShaders;
		eastl::vector<FArdaInductorPipelineContribution> mStages;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
	};

	FArdaRHIStatus FArdaRasterTriangleNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHITextureDesc D;
		D.mWidth = P.mWidth;
		D.mHeight = P.mHeight;
		D.mFormat = EArdaRHIFormat::RGBA8UNorm;
		D.mUsage = EArdaRHITextureUsage::RenderTarget;
		return C.Texture(P.mColor, "Color", D);
	}

	FArdaDependencyNodeMetadata FArdaRasterTriangleNode::GetMetadata()
	{
		return {"recipe.raster-triangle", 1};
	}

	eastl::string FArdaRasterTriangleNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mColor);
		Key.Value(P.mWidth);
		Key.Value(P.mHeight);
		return Key.Build();
	}

	FArdaRHIStatus FArdaRasterTriangleNode::Validate(const FArdaParameters& P)
	{
		if (!P.mWidth || !P.mHeight)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Raster target extent must be nonzero.");
		}
		return {};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaRasterTriangleNode::FArdaState>> FArdaRasterTriangleNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Raster preparation needs a device.")};
		}

		auto State = eastl::make_shared<FArdaState>();
		if (!State->mShaders.Initialize(Device))
		{
			return {{}, ShaderError(State->mShaders)};
		}

		const auto* FArdaVertex = State->mShaders.Find(FArdaRecipeRasterVertexShader::GetStaticType());
		const auto* Pixel = State->mShaders.Find(FArdaRecipeRasterPixelShader::GetStaticType());
		if (!FArdaVertex || !Pixel)
		{
			return {{}, ShaderError(State->mShaders)};
		}
		for (const auto* Shader : {FArdaVertex, Pixel})
		{
			FArdaInductorPipelineContribution Stage;
			Stage.mShader = Shader->GetShader();
			State->mStages.push_back(eastl::move(Stage));
		}
		auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		Config->mKind = EArdaPipelineStateKind::Graphics;
		auto& Desc = Config->mGraphics.mDesc;
		Desc.mRasterState.mCullMode = EArdaRHICullMode::None;
		Desc.mDepthStencilState.mbDepthTest = false;
		Desc.mDepthStencilState.mbDepthWrite = false;
		Desc.mSampleCount = 0; // ArdaInductor completes formats and samples from the framebuffer.
		State->mConfiguration = eastl::move(Config);
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaRasterTriangleNode::Describe(const FArdaParameters& P, const FArdaState& State)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
		D.mColorTargets = {P.mColor};
		D.mPipelineStages = State.mStages;
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, State.mConfiguration}};
		return D;
	}

	FArdaRHIStatus FArdaRasterTriangleNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		auto Color = C.GetTexture(P.mColor);
		if (auto S = C.GetCommands().ClearTexture(*Color, {}, {0.f, 0.f, 0.f, 1.f}); !S)
		{
			return S;
		}

		// Supply dynamic draw inputs; the executor fills the compiled pipeline, framebuffer, and bindings.
		FArdaRHIGraphicsState State;
		State.mViewports = {{0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f}};
		State.mScissors = {{0, int32_t(P.mWidth), 0, int32_t(P.mHeight)}};
		if (auto S = C.SetGraphicsState(State); !S)
		{
			return S;
		}
		FArdaRHIDrawArguments Draw;
		Draw.mVertexCount = 3;
		// Record only this node's draw; submission and cross-node ordering belong to the graph.
		C.GetCommands().Draw(Draw);
		return {};
	}
}
