#include "ArdaRHITestPch.h"
#include "Nodes/ArdaTriangleDrawNode.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

namespace arda
{
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

	struct FArdaTriangleDrawNode::FArdaState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;

		FArdaRHIStatus ShaderError() const
		{
			const auto Diagnostics = mShaderMap.GetDiagnostics();
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    Diagnostics.empty() ? "Triangle node shader initialization failed."
			                        : Diagnostics.back().mMessage.c_str());
		}

		FArdaRHIStatus Initialize()
		{
			// The shader map owns compilation, artifact loading, and RHI shader creation.
			if (!mShaderMap.Initialize(mDevice))
			{
				return ShaderError();
			}

			const auto* VertexShader = mShaderMap.Find(FArdaTriangleVertexShader::GetStaticType());
			const auto* PixelShader = mShaderMap.Find(FArdaTrianglePixelShader::GetStaticType());
			if (!VertexShader || !PixelShader)
			{
				return ShaderError();
			}

			// Describe the vertex stream once; graph compilation completes attachment formats.
			eastl::vector<FArdaRHIVertexAttributeDesc> Attributes(2);
			Attributes[0].mSemanticName = "POSITION";
			Attributes[0].mFormat = EArdaRHIFormat::RG32Float;
			Attributes[0].mOffset = offsetof(FArdaTriangleVertex, mPosition);
			Attributes[0].mElementStride = sizeof(FArdaTriangleVertex);
			Attributes[1].mSemanticName = "COLOR";
			Attributes[1].mFormat = EArdaRHIFormat::RGB32Float;
			Attributes[1].mOffset = offsetof(FArdaTriangleVertex, mColor);
			Attributes[1].mElementStride = sizeof(FArdaTriangleVertex);
			auto InputLayout = mDevice->CreateInputLayout(Attributes);
			if (!InputLayout)
			{
				return InputLayout.mStatus;
			}

			FArdaRHIGraphicsPipelineDesc FixedState;
			FixedState.mDepthStencilState.mbDepthTest = false;
			FixedState.mDepthStencilState.mbDepthWrite = false;
			FixedState.mRasterState.mCullMode = EArdaRHICullMode::None;
			FixedState.mSampleCount = 0;
			FixedState.mDebugName = "Triangle pipeline";

			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = EArdaPipelineStateKind::Graphics;
			Configuration->mGraphics = FArdaGraphicsPipelineStateInitializer::FromGlobalShaders(*VertexShader,
			    PixelShader,
			    InputLayout.mValue,
			    FixedState);
			mConfiguration = eastl::move(Configuration);
			return {};
		}
	};

	FArdaDependencyNodeMetadata FArdaTriangleDrawNode::GetMetadata()
	{
		return {"example.triangle.draw", 1};
	}

	eastl::string FArdaTriangleDrawNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(P.mColor)
		    .Resource(P.mVertices)
		    .Resource(P.mIndices)
		    .Value(P.mWidth)
		    .Value(P.mHeight)
		    .Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTriangleDrawNode::FArdaState>> FArdaTriangleDrawNode::Prepare(
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

	FArdaDependencyNodeDesc FArdaTriangleDrawNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc R;
		R.mAccesses = {{P.mVertices, EArdaDependencyAccess::Read, EArdaRHIResourceState::VertexBuffer},
		    {P.mIndices, EArdaDependencyAccess::Read, EArdaRHIResourceState::IndexBuffer},
		    {P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
		R.mColorTargets = {P.mColor};
		R.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Prepared.mConfiguration}};
		return R;
	}

	FArdaRHIStatus FArdaTriangleDrawNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		auto Status = C.GetCommands().ClearTexture(*C.GetTexture(P.mColor), {}, {0.025f, 0.035f, 0.06f, 1.f});
		if (!Status)
		{
			return Status;
		}

		// Supply dynamic draw inputs; the executor fills the compiled pipeline, framebuffer, and bindings.
		FArdaRHIGraphicsState State;
		State.mVertexBuffers.push_back({C.GetBuffer(P.mVertices), 0, 0});
		State.mIndexBuffer = C.GetBuffer(P.mIndices);
		State.mIndexFormat = EArdaRHIFormat::R16UInt;
		State.mViewports.push_back({0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f});
		State.mScissors.push_back({0, int32_t(P.mWidth), 0, int32_t(P.mHeight)});
		Status = C.SetGraphicsState(State);
		if (!Status)
		{
			return Status;
		}

		// Record only this node's draw; submission and cross-node ordering belong to the graph.
		C.GetCommands().DrawIndexed({3});
		return {};
	}
}
