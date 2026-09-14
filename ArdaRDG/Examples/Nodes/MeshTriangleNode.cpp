#include "MeshTriangleNode.h"
#include "RecipeNodeSupport.h"

namespace arda
{
	class FArdaRecipeMeshShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaRecipeMeshShader);
	};

	class FArdaRecipePixelShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaRecipePixelShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaRecipeMeshShader,
	    ARDA_RDG_RECIPE_SHADER_DIR "/MeshTriangle.hlsl",
	    "RecipeMesh",
	    "RecipeMS",
	    EArdaRHIShaderStage::Mesh)
	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaRecipePixelShader,
	    ARDA_RDG_RECIPE_SHADER_DIR "/MeshTriangle.hlsl",
	    "RecipePixel",
	    "RecipePS",
	    EArdaRHIShaderStage::Pixel)

	struct FArdaMeshTriangleNode::FArdaState
	{
		FArdaGlobalShaderMap mShaders;
		eastl::vector<FArdaInductorPipelineContribution> mStages;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
	};

	FArdaRHIStatus FArdaMeshTriangleNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHITextureDesc D;
		D.mWidth = P.mWidth;
		D.mHeight = P.mHeight;
		D.mFormat = EArdaRHIFormat::RGBA8UNorm;
		D.mUsage = EArdaRHITextureUsage::RenderTarget;
		return C.Texture(P.mColor, "Color", D);
	}

	FArdaDependencyNodeRequirements FArdaMeshTriangleNode::GetRequirements(const FArdaParameters&)
	{
		FArdaDependencyNodeRequirements R;
		R.mFeatures.mbRequireMeshShaders = true;
		return R;
	}

	FArdaDependencyNodeMetadata FArdaMeshTriangleNode::GetMetadata()
	{
		return {"recipe.mesh-triangle", 1};
	}

	eastl::string FArdaMeshTriangleNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mColor);
		Key.Value(P.mWidth);
		Key.Value(P.mHeight);
		return Key.Build();
	}

	FArdaRHIStatus FArdaMeshTriangleNode::Validate(const FArdaParameters& P)
	{
		if (!P.mWidth || !P.mHeight)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Mesh target extent must be nonzero.");
		}
		return {};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaMeshTriangleNode::FArdaState>> FArdaMeshTriangleNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Mesh preparation needs a device.")};
		}

		auto State = eastl::make_shared<FArdaState>();
		if (!State->mShaders.Initialize(Device))
		{
			return {{}, ShaderError(State->mShaders)};
		}

		const auto* Mesh = State->mShaders.Find(FArdaRecipeMeshShader::GetStaticType());
		const auto* Pixel = State->mShaders.Find(FArdaRecipePixelShader::GetStaticType());
		if (!Mesh || !Pixel)
		{
			return {{}, ShaderError(State->mShaders)};
		}
		for (const auto* Shader : {Mesh, Pixel})
		{
			FArdaInductorPipelineContribution Stage;
			Stage.mShader = Shader->GetShader();
			State->mStages.push_back(eastl::move(Stage));
		}
		auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		Config->mKind = EArdaPipelineStateKind::Meshlet;
		auto& Desc = Config->mMeshlet.mDesc;
		Desc.mRasterState.mCullMode = EArdaRHICullMode::None;
		Desc.mDepthStencilState.mbDepthTest = false;
		Desc.mDepthStencilState.mbDepthWrite = false;
		Desc.mSampleCount = 0; // ArdaInductor completes formats and samples from the framebuffer.
		State->mConfiguration = eastl::move(Config);
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaMeshTriangleNode::Describe(const FArdaParameters& P, const FArdaState& State)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
		D.mColorTargets = {P.mColor};
		D.mPipelineStages = State.mStages;
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Meshlet, {}, State.mConfiguration}};
		return D;
	}

	FArdaRHIStatus FArdaMeshTriangleNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		auto Color = C.GetTexture(P.mColor);
		if (auto S = C.GetCommands().ClearTexture(*Color, {}, {0.f, 0.f, 0.f, 1.f}); !S)
		{
			return S;
		}
		FArdaRHIMeshletState State;
		State.mViewports = {{0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f}};
		State.mScissors = {{0, int32_t(P.mWidth), 0, int32_t(P.mHeight)}};
		if (auto S = C.SetMeshletState(State); !S)
		{
			return S;
		}

		return C.GetCommands().DispatchMesh(1, 1, 1);
	}
}
