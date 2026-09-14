#include "Nodes/ArdaPixelSortPresentNode.h"
#include "ArdaPixelSortNodeInternal.h"

namespace arda
{
	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaPixelSortPresentShaderParameters)
		ARDA_SHADER_CONSTANT_BUFFER(mConstants, 0, 0, EArdaRHIShaderStage::Pixel)
		ARDA_SHADER_TEXTURE_SRV(mSorted, 0, 0, EArdaRHIShaderStage::Pixel)
		ARDA_SHADER_TEXTURE_SRV(mNoise, 1, 0, EArdaRHIShaderStage::Pixel)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	class FArdaPixelSortVertexShader final : public FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaPixelSortVertexShader);
	};

	class FArdaPixelSortPixelShader final : public FArdaGlobalShader
	{
	public:
		using FArdaParameters = FArdaPixelSortPresentShaderParameters;
		ARDA_DECLARE_GLOBAL_SHADER(FArdaPixelSortPixelShader);
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaPixelSortVertexShader,
	    GetPixelSortShaderSource(),
	    "PixelSortVertex",
	    "PresentVS",
	    EArdaRHIShaderStage::Vertex)
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaPixelSortPixelShader,
	    GetPixelSortShaderSource(),
	    "PixelSortPixel",
	    "PresentPS",
	    EArdaRHIShaderStage::Pixel)

	struct FArdaPixelSortPresentNode::FArdaState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mPresent;

		FArdaRHIStatus Initialize()
		{
			// Keep shader discovery and loading inside the node's prepared state.
			if (!mShaderMap.Initialize(mDevice))
			{
				return PixelSortShaderError(mShaderMap);
			}

			const auto* Vertex = mShaderMap.Find(FArdaPixelSortVertexShader::GetStaticType());
			const auto* Pixel = mShaderMap.Find(FArdaPixelSortPixelShader::GetStaticType());
			if (!Vertex || !Pixel)
			{
				return PixelSortShaderError(mShaderMap);
			}

			// SV_VertexID supplies the fullscreen triangle; compilation fills target formats and samples.
			FArdaRHIGraphicsPipelineDesc FixedState;
			FixedState.mSampleCount = 0;
			FixedState.mDepthStencilState.mbDepthTest = false;
			FixedState.mDepthStencilState.mbDepthWrite = false;
			FixedState.mRasterState.mCullMode = EArdaRHICullMode::None;
			FixedState.mDebugName = "PixelSort presentation";

			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = EArdaPipelineStateKind::Graphics;
			Configuration->mGraphics =
			    FArdaGraphicsPipelineStateInitializer::FromGlobalShaders(*Vertex, Pixel, {}, FixedState);
			mPresent = eastl::move(Configuration);
			return {};
		}
	};

	FArdaDependencyNodeMetadata FArdaPixelSortPresentNode::GetMetadata()
	{
		return {"example.pixel-sort.present", 1};
	}

	eastl::string FArdaPixelSortPresentNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(P.mConstants)
		    .Resource(P.mNoise)
		    .Resource(P.mSorted)
		    .Resource(P.mColor)
		    .Value(P.mWidth)
		    .Value(P.mHeight)
		    .Build();
	}

	FArdaRHIStatus FArdaPixelSortPresentNode::Validate(const FArdaParameters& P)
	{
		return ValidatePixelSortExtent(P.mWidth, P.mHeight);
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaPixelSortPresentNode::FArdaState>> FArdaPixelSortPresentNode::Prepare(
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

	FArdaDependencyNodeDesc FArdaPixelSortPresentNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.BindShader<FArdaPixelSortPresentShaderParameters>(
		    {{"mConstants", P.mConstants}, {"mNoise", P.mNoise}, {"mSorted", P.mSorted}});
		D.mAccesses = {{P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
		D.mColorTargets = {P.mColor};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Prepared.mPresent}};

		return D;
	}

	FArdaRHIStatus FArdaPixelSortPresentNode::Record(FArdaDependencyExecutionContext& C,
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
