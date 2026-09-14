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
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_TEXTURE_SRV(mSource, 0, 0, arda::EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTerrainOverlayPixelShader);
	};

	struct FArdaTerrainOverlayNode::FArdaState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;

		FArdaRHIStatus Initialize()
		{
			// Load the node-owned shader types through the common device shader map.
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

	eastl::string FArdaTerrainOverlayNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;

		Key.Resource(P.mSource);
		Key.Resource(P.mColor);
		Key.Value(P.mWidth);
		Key.Value(P.mHeight);

		return Key.Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTerrainOverlayNode::FArdaState>> FArdaTerrainOverlayNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This node requires an initialized device.")};
		}

		auto State = eastl::make_shared<FArdaState>();
		State->mDevice = eastl::move(Device);

		// Publish prepared state only after all node-owned setup succeeds.
		auto Status = State->Initialize();
		if (!Status)
		{
			return {{}, eastl::move(Status)};
		}

		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaTerrainOverlayNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.BindShader<FArdaTerrainOverlayPixelShader::FArdaParameters>({{"mSource", P.mSource}});
		D.mAccesses = {{P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Prepared.mConfiguration}};
		D.mEstimatedCost = 8;
		D.mColorTargets = {P.mColor};
		return D;
	}

	FArdaRHIStatus FArdaTerrainOverlayNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		// Supply dynamic draw inputs; the executor fills the compiled pipeline, framebuffer, and bindings.
		FArdaRHIGraphicsState State;
		State.mViewports = {{0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f}};
		State.mScissors = {{0, int32_t(P.mWidth), 0, int32_t(P.mHeight)}};
		if (auto S = C.SetGraphicsState(State); !S)
		{
			return S;
		}

		// Record only this node's draw; submission and cross-node ordering belong to the graph.
		C.GetCommands().Draw({3});
		return {};
	}
}
