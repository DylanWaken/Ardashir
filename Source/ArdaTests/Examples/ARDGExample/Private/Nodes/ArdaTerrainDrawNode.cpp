#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainDrawNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	class FArdaTerrainVertexShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
			ARDA_SHADER_CONSTANT_BUFFER(mCamera, 0, 0, arda::EArdaRHIShaderStage::Vertex)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTerrainVertexShader);
	};

	class FArdaTerrainPixelShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
			ARDA_SHADER_TEXTURE_SRV(mHeightmap, 0, 0, arda::EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTerrainPixelShader);
	};

	struct FArdaTerrainDrawNode::FState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
		FArdaRHIBindingLayoutRef mPixelLayout;
		FArdaRHIBindingLayoutRef mCameraLayout;

		FArdaRHIStatus Initialize()
		{
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
			eastl::vector<arda::FArdaRHIVertexAttributeDesc> attributes(2);
			attributes[0].mSemanticName = "POSITION";
			attributes[0].mFormat = arda::EArdaRHIFormat::RGB32Float;
			attributes[0].mOffset = offsetof(FArdaTerrainVertex, mPosition);
			attributes[0].mElementStride = sizeof(FArdaTerrainVertex);
			attributes[1].mSemanticName = "HEIGHT";
			attributes[1].mFormat = arda::EArdaRHIFormat::R32Float;
			attributes[1].mOffset = offsetof(FArdaTerrainVertex, mHeight);
			attributes[1].mElementStride = sizeof(FArdaTerrainVertex);
			auto inputLayout = mDevice->CreateInputLayout(attributes);
			if (!inputLayout)
			{
				return inputLayout.mStatus;
			}
			const auto mTerrainInputLayout = eastl::move(inputLayout.mValue);

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

			mPixelLayout = mTerrainPixelShader->GetBindingLayouts()[0];
			mCameraLayout = mTerrainVertexShader->GetBindingLayouts()[0];
			mConfiguration = eastl::move(Configuration);
			return {};
		}
	};

	struct FArdaTerrainDrawNode::FInstanceState
	{
		FArdaRHIBindingSetRef mCamera;
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

	FArdaDependencyNodeMetadata FArdaTerrainDrawNode::GetMetadata()
	{
		return {"example.terrain.draw", 1};
	}

	eastl::string FArdaTerrainDrawNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string Key;

		AppendResource(Key, P.mHeightmap);
		AppendResource(Key, P.mVertices);
		AppendResource(Key, P.mIndices);
		AppendResource(Key, P.mCamera);
		AppendResource(Key, P.mColor);
		AppendResource(Key, P.mDepth);
		Append(Key, P.mWidth);
		Append(Key, P.mHeight);

		return Key;
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTerrainDrawNode::FState>> FArdaTerrainDrawNode::Prepare(
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

	TArdaRHIResult<eastl::shared_ptr<FArdaTerrainDrawNode::FInstanceState>> FArdaTerrainDrawNode::CreateInstance(
	    FArdaRHIDeviceRef,
	    const FParameters&,
	    const FState&)
	{
		return {eastl::make_shared<FInstanceState>(), {}};
	}

	FArdaDependencyNodeDesc FArdaTerrainDrawNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mHeightmap, EArdaDependencyAccess::Read, EArdaRHIResourceState::PixelShaderResource},
		    {P.mVertices, EArdaDependencyAccess::Read, EArdaRHIResourceState::VertexBuffer},
		    {P.mIndices, EArdaDependencyAccess::Read, EArdaRHIResourceState::IndexBuffer},
		    {P.mCamera, EArdaDependencyAccess::Read, EArdaRHIResourceState::ConstantBuffer},
		    {P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget},
		    {P.mDepth, EArdaDependencyAccess::Write, EArdaRHIResourceState::DepthWrite}};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Prepared.mConfiguration}};
		D.mEstimatedCost = 8;
		D.mColorTargets = {P.mColor};
		D.mDepthTarget = P.mDepth;
		return D;
	}

	FArdaRHIStatus FArdaTerrainDrawNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		FArdaTerrainPixelShader::FParameters Pixel;
		Pixel.mHeightmap = C.GetTexture(P.mHeightmap);
		FArdaRHIBindingSetRef PixelBindings;
		auto Status = CreateBindings(C, Pixel, Prepared.mPixelLayout, PixelBindings);
		if (Status)
		{
			Status = C.GetCommands().ClearTexture(*C.GetTexture(P.mColor), {}, {0.004f, 0.007f, 0.009f, 1.f});
		}
		if (Status)
		{
			Status = C.GetCommands().ClearDepthStencilTexture(*C.GetTexture(P.mDepth), {}, true, 0.f, false, 0);
		}
		if (!Status)
		{
			return Status;
		}
		FArdaTerrainVertexShader::FParameters Camera;
		Camera.mCamera = C.GetBuffer(P.mCamera);
		auto& CameraBindings = InstanceState.mCamera;
		if (!CameraBindings || CameraBindings->GetDesc().mItems[0].mResource.Get() != Camera.mCamera.Get())
		{
			Status = CreateBindings(C, Camera, Prepared.mCameraLayout, CameraBindings);
			if (!Status)
			{
				return Status;
			}
		}
		FArdaRHIGraphicsState State;
		State.mPipeline = C.GetPipeline()->mGraphics;
		State.mFramebuffer = C.GetFramebuffer();
		State.mBindings = {CameraBindings, PixelBindings};
		State.mVertexBuffers.push_back({C.GetBuffer(P.mVertices), 0, 0});
		State.mIndexBuffer = C.GetBuffer(P.mIndices);
		State.mIndexFormat = EArdaRHIFormat::R32UInt;
		State.mViewports.push_back({0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f});
		State.mScissors.push_back({0, int32_t(P.mWidth), 0, int32_t(P.mHeight)});
		Status = C.GetCommands().SetGraphicsState(State);
		if (!Status)
		{
			return Status;
		}
		C.GetCommands().DrawIndexed({ArdaTerrainIndexCount});

		return {};
	}
}
