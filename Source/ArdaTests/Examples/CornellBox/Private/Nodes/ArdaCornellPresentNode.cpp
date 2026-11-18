#include "ArdaExampleNodeState.h"
#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellPresentNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaCornellPresentNode, CornellPresent);

	class FArdaCornellPresentVertexShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellPresentVertexShader);
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

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellPresentNode::FArdaState>> FArdaCornellPresentNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		return PrepareExampleNodeState<FArdaState>(eastl::move(Device));
	}

	FArdaDependencyNodeDesc FArdaCornellPresentNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;

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
