#include "ArdaRHITestPch.h"
#include "Nodes/ArdaTriangleDrawNode.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaTriangleVertexNode, TriangleVS);
	ARDA_DEFINE_GRAPH_NODE(FArdaTriangleRasterNode, TriangleRasterize);
	ARDA_DEFINE_GRAPH_NODE(FArdaTrianglePixelNode, TrianglePS);

	class FArdaTriangleVertexShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTriangleVertexShader);
	};

	class FArdaTrianglePixelShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTrianglePixelShader);
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaTriangleVertexShader,
	    ARDA_RHI_TEST_SHADER_SOURCE_DIR "/ArdaTriangle.hlsl",
	    "TriangleVS",
	    "VSMain",
	    EArdaRHIShaderStage::Vertex)
	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaTrianglePixelShader,
	    ARDA_RHI_TEST_SHADER_SOURCE_DIR "/ArdaTriangle.hlsl",
	    "TrianglePS",
	    "PSMain",
	    EArdaRHIShaderStage::Pixel)

	namespace
	{
		FArdaRHIStatus TriangleShaderError(const FArdaGlobalShaderMap& Shaders)
		{
			const auto Diagnostics = Shaders.GetDiagnostics();
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    Diagnostics.empty() ? "Triangle shader initialization failed." : Diagnostics.back().mMessage.c_str());
		}
	}

	struct FArdaTriangleVertexNode::FArdaState
	{
		FArdaGlobalShaderMap mShaders;
		FArdaInductorPipelineContribution mStage;
		FArdaRHIInputLayoutRef mInputLayout;
	};

	FArdaDependencyNodeMetadata FArdaTriangleVertexNode::GetMetadata()
	{
		return {"example.triangle.vertex", 1};
	}

	eastl::string FArdaTriangleVertexNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder().Resource(P.mVertices).Resource(P.mIndices).Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTriangleVertexNode::FArdaState>> FArdaTriangleVertexNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Triangle vertex stage requires a device.")};
		}
		auto State = eastl::make_shared<FArdaState>();
		if (!State->mShaders.Initialize(Device))
		{
			return {{}, TriangleShaderError(State->mShaders)};
		}
		const auto* Shader = State->mShaders.Find(FArdaTriangleVertexShader::GetStaticType());
		if (!Shader)
		{
			return {{}, TriangleShaderError(State->mShaders)};
		}
		State->mStage.mShader = Shader->GetShader();
		State->mStage.mBindingLayouts = Shader->GetBindingLayouts();

		eastl::vector<FArdaRHIVertexAttributeDesc> Attributes(2);
		Attributes[0].mSemanticName = "POSITION";
		Attributes[0].mFormat = EArdaRHIFormat::RG32Float;
		Attributes[0].mOffset = offsetof(FArdaTriangleVertex, mPosition);
		Attributes[0].mElementStride = sizeof(FArdaTriangleVertex);
		Attributes[1].mSemanticName = "COLOR";
		Attributes[1].mFormat = EArdaRHIFormat::RGB32Float;
		Attributes[1].mOffset = offsetof(FArdaTriangleVertex, mColor);
		Attributes[1].mElementStride = sizeof(FArdaTriangleVertex);
		auto InputLayout = Device->CreateInputLayout(Attributes);
		if (!InputLayout)
		{
			return {{}, InputLayout.mStatus};
		}
		State->mInputLayout = eastl::move(InputLayout.mValue);
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaTriangleVertexNode::Describe(const FArdaParameters& P, const FArdaState& State)
	{
		FArdaDependencyNodeDesc Desc;
		Desc.mAccesses = {{P.mVertices, EArdaDependencyAccess::Read, EArdaRHIResourceState::VertexBuffer},
		    {P.mIndices, EArdaDependencyAccess::Read, EArdaRHIResourceState::IndexBuffer}};
		Desc.mPipelineStages = {State.mStage};
		Desc.mGraphicsInputLayout = State.mInputLayout;
		return Desc;
	}

	FArdaRHIStatus FArdaTriangleVertexNode::ConfigureGraphics(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState&,
	    FArdaInstanceState&,
	    FArdaGraphicsWork& Work)
	{
		Work.mState.mVertexBuffers.push_back({C.GetBuffer(P.mVertices), 0, 0});
		Work.mState.mIndexBuffer = C.GetBuffer(P.mIndices);
		Work.mState.mIndexFormat = EArdaRHIFormat::R16UInt;
		Work.mDraw.mVertexCount = 3;
		Work.mbIndexed = true;
		return {};
	}

	struct FArdaTriangleRasterNode::FArdaState
	{
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
	};

	FArdaDependencyNodeMetadata FArdaTriangleRasterNode::GetMetadata()
	{
		return {"example.triangle.rasterize", 1};
	}

	eastl::string FArdaTriangleRasterNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder().Value(P.mWidth).Value(P.mHeight).Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTriangleRasterNode::FArdaState>> FArdaTriangleRasterNode::Prepare(
	    FArdaRHIDeviceRef)
	{
		auto State = eastl::make_shared<FArdaState>();
		auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		Config->mKind = EArdaPipelineStateKind::Graphics;
		auto& Desc = Config->mGraphics.mDesc;
		Desc.mDepthStencilState.mbDepthTest = false;
		Desc.mDepthStencilState.mbDepthWrite = false;
		Desc.mRasterState.mCullMode = EArdaRHICullMode::None;
		Desc.mSampleCount = 0;
		Desc.mDebugName = "Triangle pipeline";
		State->mConfiguration = eastl::move(Config);
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaTriangleRasterNode::Describe(const FArdaParameters&, const FArdaState& State)
	{
		FArdaDependencyNodeDesc Desc;
		FArdaInductorPipelineContribution Raster;
		Raster.mConfiguration = State.mConfiguration;
		Desc.mPipelineStages = {eastl::move(Raster)};
		return Desc;
	}

	FArdaRHIStatus FArdaTriangleRasterNode::ConfigureGraphics(FArdaDependencyExecutionContext&,
	    const FArdaParameters& P,
	    const FArdaState&,
	    FArdaInstanceState&,
	    FArdaGraphicsWork& Work)
	{
		Work.mState.mViewports = {{0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f}};
		Work.mState.mScissors = {{0, int32_t(P.mWidth), 0, int32_t(P.mHeight)}};
		return {};
	}

	struct FArdaTrianglePixelNode::FArdaState
	{
		FArdaGlobalShaderMap mShaders;
		FArdaInductorPipelineContribution mStage;
	};

	FArdaDependencyNodeMetadata FArdaTrianglePixelNode::GetMetadata()
	{
		return {"example.triangle.pixel", 1};
	}

	eastl::string FArdaTrianglePixelNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder().Resource(P.mColor).Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTrianglePixelNode::FArdaState>> FArdaTrianglePixelNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Triangle pixel stage requires a device.")};
		}
		auto State = eastl::make_shared<FArdaState>();
		if (!State->mShaders.Initialize(Device))
		{
			return {{}, TriangleShaderError(State->mShaders)};
		}
		const auto* Shader = State->mShaders.Find(FArdaTrianglePixelShader::GetStaticType());
		if (!Shader)
		{
			return {{}, TriangleShaderError(State->mShaders)};
		}
		State->mStage.mShader = Shader->GetShader();
		State->mStage.mBindingLayouts = Shader->GetBindingLayouts();
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaTrianglePixelNode::Describe(const FArdaParameters& P, const FArdaState& State)
	{
		FArdaDependencyNodeDesc Desc;
		Desc.mAccesses = {{P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
		Desc.mColorTargets = {P.mColor};
		Desc.mPipelineStages = {State.mStage};
		return Desc;
	}

	FArdaRHIStatus FArdaTrianglePixelNode::ConfigureGraphics(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState&,
	    FArdaInstanceState&,
	    FArdaGraphicsWork&)
	{
		return C.GetCommands().ClearTexture(*C.GetTexture(P.mColor), {}, {0.025f, 0.035f, 0.06f, 1.f});
	}
}
