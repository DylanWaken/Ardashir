#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellPresentNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	class FArdaCornellPresentVertexShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellPresentVertexShader);
	};

	class FArdaCornellPresentPixelShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_TEXTURE_SRV(mAccumulation, 0, 0, arda::EArdaRHIShaderStage::Pixel)
			ARDA_SHADER_UNIFORM_BUFFER(mFrame, 0, 0, arda::EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellPresentPixelShader);
	};

	struct FArdaCornellPresentNode::FArdaState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::vector<FArdaInductorPipelineContribution> mStages;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;

		FArdaRHIStatus Initialize()
		{
			// Load the node-owned shader types through the common device shader map.
			if (!mShaderMap.Initialize(mDevice))
			{
				return CornellShaderError(mShaderMap);
			}

			const auto* Shader0 = mShaderMap.Find(FArdaCornellPresentVertexShader::GetStaticType());
			if (!Shader0)
			{
				return CornellShaderError(mShaderMap);
			}
			mStages.push_back(CornellShaderStage(Shader0));
			const auto* Shader1 = mShaderMap.Find(FArdaCornellPresentPixelShader::GetStaticType());
			if (!Shader1)
			{
				return CornellShaderError(mShaderMap);
			}
			mStages.push_back(CornellShaderStage(Shader1));
			auto Present = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Present->mKind = EArdaPipelineStateKind::Graphics;
			Present->mGraphics.mDesc.mRasterState.mCullMode = EArdaRHICullMode::None;
			Present->mGraphics.mDesc.mDepthStencilState.mbDepthTest = false;
			Present->mGraphics.mDesc.mDepthStencilState.mbDepthWrite = false;
			Present->mGraphics.mDesc.mSampleCount = 0;
			mConfiguration = eastl::move(Present);
			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaCornellPresentVertexShader,
	    "/ArdaTests/CornellBox/CornellPresent.hlsl",
	    "CornellPresentVS",
	    "CornellPresentVS",
	    arda::EArdaRHIShaderStage::Vertex)
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaCornellPresentPixelShader,
	    "/ArdaTests/CornellBox/CornellPresent.hlsl",
	    "CornellPresentPS",
	    "CornellPresentPS",
	    arda::EArdaRHIShaderStage::Pixel)

	FArdaDependencyNodeMetadata FArdaCornellPresentNode::GetMetadata()
	{
		return {"cornell.present", 1};
	}

	eastl::string FArdaCornellPresentNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mAccumulation);
		Key.Resource(P.mConstants);
		Key.Resource(P.mColor);
		Key.Value(P.mWidth);
		Key.Value(P.mHeight);
		Key.Value(P.mWorkspaceBytes);
		return Key.Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellPresentNode::FArdaState>> FArdaCornellPresentNode::Prepare(
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

	FArdaDependencyNodeDesc FArdaCornellPresentNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		D.BindShader<FArdaCornellPresentPixelShader::FArdaParameters>(
		    {{"mAccumulation", P.mAccumulation}, {"mFrame", P.mConstants}});
		D.mAccesses = {{P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
		D.mColorTargets = {P.mColor};

		D.mPipelineStages = Prepared.mStages;
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Prepared.mConfiguration}};

		return D;
	}

	FArdaRHIStatus FArdaCornellPresentNode::Record(FArdaDependencyExecutionContext& C,
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
