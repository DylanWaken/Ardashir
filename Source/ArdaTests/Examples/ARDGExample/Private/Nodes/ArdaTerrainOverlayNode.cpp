#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainOverlayNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	class FArdaTerrainOverlayVertexShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTerrainOverlayVertexShader);
	};

	class FArdaTerrainOverlayPixelShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
			ARDA_SHADER_TEXTURE_SRV(mSource, 0, 0, arda::EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTerrainOverlayPixelShader);
	};

	struct FArdaTerrainOverlayNode::FState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
		FArdaRHIBindingLayoutRef mLayout;

		FArdaRHIStatus Initialize()
		{
			if (!mShaderMap.Initialize(mDevice))
			{
				return TerrainShaderError(mShaderMap);
			}
			const auto* mOverlayVertexShader = mShaderMap.Find(FArdaTerrainOverlayVertexShader::GetStaticType());
			const auto* mOverlayPixelShader = mShaderMap.Find(FArdaTerrainOverlayPixelShader::GetStaticType());
			if (!mOverlayVertexShader || !mOverlayPixelShader)
			{
				return TerrainShaderError(mShaderMap);
			}
			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = EArdaPipelineStateKind::Graphics;
			arda::FArdaRHIGraphicsPipelineDesc overlayFixedState;
			overlayFixedState.mRasterState.mCullMode = arda::EArdaRHICullMode::None;
			overlayFixedState.mDepthStencilState.mbDepthTest = false;
			overlayFixedState.mDepthStencilState.mbDepthWrite = false;
			// The overlay reads the preceding color value and writes a new graph value.
			overlayFixedState.mSampleCount = 0;
			overlayFixedState.mDebugName = "Terrain overlay pipeline";
			Configuration->mGraphics =
			    arda::FArdaGraphicsPipelineStateInitializer::FromGlobalShaders(*mOverlayVertexShader,
			        mOverlayPixelShader,
			        {},
			        overlayFixedState);
			mLayout = mOverlayPixelShader->GetBindingLayouts()[0];
			mConfiguration = eastl::move(Configuration);
			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaTerrainOverlayVertexShader,
	    "/ArdaTests/ARDGExample/ArdaTerrain.hlsl",
	    "TerrainOverlayVS",
	    "TerrainOverlayVS",
	    arda::EArdaRHIShaderStage::Vertex)
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaTerrainOverlayPixelShader,
	    "/ArdaTests/ARDGExample/ArdaTerrain.hlsl",
	    "TerrainOverlayPS",
	    "TerrainOverlayPS",
	    arda::EArdaRHIShaderStage::Pixel)

	FArdaDependencyNodeMetadata FArdaTerrainOverlayNode::GetMetadata()
	{
		return {"example.terrain.overlay", 1};
	}

	eastl::string FArdaTerrainOverlayNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string Key;

		AppendResource(Key, P.mSource);
		AppendResource(Key, P.mColor);
		Append(Key, P.mWidth);
		Append(Key, P.mHeight);

		return Key;
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTerrainOverlayNode::FState>> FArdaTerrainOverlayNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This node requires an initialized device.")};
		}
		auto State = eastl::make_shared<FState>();
		State->mDevice = eastl::move(Device);
		auto Status = State->Initialize();
		if (!Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaTerrainOverlayNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::PixelShaderResource},
		    {P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Prepared.mConfiguration}};
		D.mEstimatedCost = 8;
		D.mColorTargets = {P.mColor};
		return D;
	}

	FArdaRHIStatus FArdaTerrainOverlayNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		FArdaTerrainOverlayPixelShader::FParameters Shader;
		Shader.mSource = C.GetTexture(P.mSource);
		FArdaRHIBindingSetRef Bindings;
		auto Status = CreateBindings(C, Shader, Prepared.mLayout, Bindings);
		if (!Status)
		{
			return Status;
		}
		FArdaRHIGraphicsState State;
		State.mPipeline = C.GetPipeline()->mGraphics;
		State.mFramebuffer = C.GetFramebuffer();
		State.mBindings = {Bindings};
		State.mViewports = {{0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f}};
		State.mScissors = {{0, int32_t(P.mWidth), 0, int32_t(P.mHeight)}};
		Status = C.GetCommands().SetGraphicsState(State);
		if (!Status)
		{
			return Status;
		}
		C.GetCommands().Draw({3});
		return {};
	}
}
