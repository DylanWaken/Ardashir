#include "ArdaExampleNodeState.h"
#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellAccumulateNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaCornellAccumulateNode, CornellAccumulate);

	struct FArdaCornellAccumulateNode::FArdaState
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

			const auto* Shader0 = mShaderMap.Find(FArdaAccumulateCornellSamplesShader::GetStaticType());
			if (!Shader0)
			{
				return CornellShaderError(mShaderMap);
			}
			mStages.push_back(CornellShaderStage(Shader0));

			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaAccumulateCornellSamplesShader,
	    "/ArdaTests/CornellBox/CornellAccumulate.hlsl",
	    "CornellAccumulateCS",
	    "CornellAccumulateCS",
	    arda::EArdaRHIShaderStage::Compute)

	FArdaDependencyNodeMetadata FArdaCornellAccumulateNode::GetMetadata()
	{
		return {"cornell.accumulate", 1};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellAccumulateNode::FArdaState>> FArdaCornellAccumulateNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		return PrepareExampleNodeState<FArdaState>(eastl::move(Device));
	}

	FArdaDependencyNodeDesc FArdaCornellAccumulateNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;

		D.mPipelineStages = Prepared.mStages;
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, Prepared.mConfiguration}};

		return D;
	}

	FArdaRHIStatus FArdaCornellAccumulateNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		// Bind the executor-prepared pipeline and shader arguments before dispatching this operation.
		if (auto S = C.SetComputeState(); !S)
		{
			return S;
		}

		C.GetCommands().Dispatch(P.mGroupCountX, P.mGroupCountY, 1);
		return {};
	}
}
