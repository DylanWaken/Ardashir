#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainDrawNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	class FArdaTerrainVertexShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_CONSTANT_BUFFER(mCamera, 0, 0, arda::EArdaRHIShaderStage::Vertex)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTerrainVertexShader);
	};

	class FArdaTerrainPixelShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_TEXTURE_SRV(mHeightmap, 0, 0, arda::EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTerrainPixelShader);
	};

	struct FArdaTerrainDrawNode::FArdaState
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

			const auto* mTerrainVertexShader = mShaderMap.Find(FArdaTerrainVertexShader::GetStaticType());
			const auto* mTerrainPixelShader = mShaderMap.Find(FArdaTerrainPixelShader::GetStaticType());
			if (!mTerrainVertexShader || !mTerrainPixelShader)
			{
				return TerrainShaderError(mShaderMap);
			}

			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = EArdaPipelineStateKind::Graphics;

			// Expose only shader-consumed attributes while retaining the compute buffer's vertex stride.
			eastl::vector<FArdaRHIVertexAttributeDesc> Attributes(1);
			Attributes[0].mSemanticName = "POSITION";
			Attributes[0].mFormat = EArdaRHIFormat::RGB32Float;
			Attributes[0].mOffset = offsetof(FArdaTerrainVertex, mPosition);
			Attributes[0].mElementStride = sizeof(FArdaTerrainVertex);
			auto InputLayout = mDevice->CreateInputLayout(Attributes);
			if (!InputLayout)
			{
				return InputLayout.mStatus;
			}
			const auto mTerrainInputLayout = eastl::move(InputLayout.mValue);

			arda::FArdaRHIGraphicsPipelineDesc terrainFixedState;
			terrainFixedState.mRasterState.mCullMode = arda::EArdaRHICullMode::None;
			terrainFixedState.mDepthStencilState.mDepthFunc = arda::EArdaRHIComparisonFunc::GreaterOrEqual;
			terrainFixedState.mSampleCount = 0;
			terrainFixedState.mDebugName = "Terrain pipeline";
			Configuration->mGraphics =
			    arda::FArdaGraphicsPipelineStateInitializer::FromGlobalShaders(*mTerrainVertexShader,
			        mTerrainPixelShader,
			        mTerrainInputLayout,
			        terrainFixedState);

			mConfiguration = eastl::move(Configuration);
			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaTerrainVertexShader,
	    "/ArdaTests/ARDGExample/ArdaTerrain.hlsl",
	    "TerrainVS",
	    "TerrainVS",
	    arda::EArdaRHIShaderStage::Vertex)
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaTerrainPixelShader,
	    "/ArdaTests/ARDGExample/ArdaTerrain.hlsl",
	    "TerrainPS",
	    "TerrainPS",
	    arda::EArdaRHIShaderStage::Pixel)

	FArdaRHIStatus FArdaTerrainDrawNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHITextureDesc D;
		D.mWidth = P.mWidth;
		D.mHeight = P.mHeight;
		D.mFormat = P.mColorFormat;
		D.mUsage = EArdaRHITextureUsage::RenderTarget | EArdaRHITextureUsage::ShaderResource;
		if (auto S = C.Texture(P.mColor, "Color", D); !S)
		{
			return S;
		}
		D.mFormat = EArdaRHIFormat::D32;
		D.mUsage = EArdaRHITextureUsage::DepthStencil;
		return C.Texture(P.mDepth, "Depth", D);
	}

	FArdaDependencyNodeMetadata FArdaTerrainDrawNode::GetMetadata()
	{
		return {"example.terrain.draw", 1};
	}

	eastl::string FArdaTerrainDrawNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;

		Key.Resource(P.mHeightmap);
		Key.Resource(P.mVertices);
		Key.Resource(P.mIndices);
		Key.Resource(P.mCamera);
		Key.Resource(P.mColor);
		Key.Resource(P.mDepth);
		Key.Value(P.mWidth);
		Key.Value(P.mHeight);
		Key.Value(uint64_t(P.mColorFormat));

		return Key.Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTerrainDrawNode::FArdaState>> FArdaTerrainDrawNode::Prepare(
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

	FArdaDependencyNodeDesc FArdaTerrainDrawNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.BindShader<FArdaTerrainPixelShader::FArdaParameters>({{"mHeightmap", P.mHeightmap}});
		D.BindShader<FArdaTerrainVertexShader::FArdaParameters>({{"mCamera", P.mCamera}});
		D.mAccesses = {{P.mVertices, EArdaDependencyAccess::Read, EArdaRHIResourceState::VertexBuffer},
		    {P.mIndices, EArdaDependencyAccess::Read, EArdaRHIResourceState::IndexBuffer},
		    {P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget},
		    {P.mDepth, EArdaDependencyAccess::Write, EArdaRHIResourceState::DepthWrite}};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Prepared.mConfiguration}};
		D.mEstimatedCost = 8;
		D.mColorTargets = {P.mColor};
		D.mDepthTarget = P.mDepth;
		return D;
	}

	FArdaRHIStatus FArdaTerrainDrawNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		if (auto S = C.GetCommands().ClearTexture(*C.GetTexture(P.mColor), {}, {0.004f, 0.007f, 0.009f, 1.f}); !S)
		{
			return S;
		}
		if (auto S = C.GetCommands().ClearDepthStencilTexture(*C.GetTexture(P.mDepth), {}, true, 0.f, false, 0); !S)
		{
			return S;
		}

		// Supply dynamic draw inputs; the executor fills the compiled pipeline, framebuffer, and bindings.
		FArdaRHIGraphicsState State;
		State.mVertexBuffers = {{C.GetBuffer(P.mVertices), 0, 0}};
		State.mIndexBuffer = C.GetBuffer(P.mIndices);
		State.mIndexFormat = EArdaRHIFormat::R32UInt;
		State.mViewports = {{0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f}};
		State.mScissors = {{0, int32_t(P.mWidth), 0, int32_t(P.mHeight)}};
		if (auto S = C.SetGraphicsState(State); !S)
		{
			return S;
		}

		// Record only this node's draw; submission and cross-node ordering belong to the graph.
		C.GetCommands().DrawIndexed({ArdaTerrainIndexCount});
		return {};
	}
}
