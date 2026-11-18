#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellTensorNodes.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaCornellTensorPackNode, CornellTensorPack);
	ARDA_DEFINE_GRAPH_NODE(FArdaCornellTensorUnpackNode, CornellTensorUnpack);
	ARDA_DEFINE_GRAPH_NODE(FArdaCornellNppdPackNode, CornellNppdPack);
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaCornellNppdPackShader,
	    "/ArdaTests/CornellBox/ArdaCornellNppdPack.hlsl",
	    "CornellNppdPackCS",
	    "MainCS",
	    EArdaRHIShaderStage::Compute)
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaCornellTensorPackShader,
	    "/ArdaTests/CornellBox/ArdaCornellTensorPack.hlsl",
	    "CornellTensorPackCS",
	    "MainCS",
	    EArdaRHIShaderStage::Compute)
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaCornellTensorUnpackShader,
	    "/ArdaTests/CornellBox/ArdaCornellTensorUnpack.hlsl",
	    "CornellTensorUnpackCS",
	    "MainCS",
	    EArdaRHIShaderStage::Compute)

	struct FArdaCornellTensorState
	{
		FArdaGlobalShaderMap mShaderMap;
		eastl::vector<FArdaInductorPipelineContribution> mStages;
	};

	namespace
	{
		template <class Shader>
		TArdaRHIResult<eastl::shared_ptr<const FArdaCornellTensorState>> PrepareTensorShader(FArdaRHIDeviceRef Device)
		{
			auto State = eastl::make_shared<FArdaCornellTensorState>();
			if (!State->mShaderMap.Initialize(Device))
			{
				return {{}, CornellShaderError(State->mShaderMap)};
			}
			const auto* Instance = State->mShaderMap.Find(Shader::GetStaticType());
			if (!Instance)
			{
				return {{}, CornellShaderError(State->mShaderMap)};
			}
			State->mStages.push_back(CornellShaderStage(Instance));
			return {eastl::move(State), {}};
		}

		FArdaDependencyNodeDesc DescribeTensorShader(const FArdaCornellTensorState& State)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelineStages = State.mStages;
			Desc.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, {}}};
			return Desc;
		}

		FArdaRHIStatus RecordTensorShader(FArdaDependencyExecutionContext& Context, uint32_t Width, uint32_t Height)
		{
			if (auto Status = Context.SetComputeState(); !Status)
			{
				return Status;
			}
			Context.GetCommands().Dispatch((Width + 7) / 8, (Height + 7) / 8, 1);
			return {};
		}
	}

	FArdaDependencyNodeMetadata FArdaCornellTensorPackNode::GetMetadata()
	{
		return {"cornell.tensor.pack", 1};
	}

	FArdaDependencyNodeMetadata FArdaCornellNppdPackNode::GetMetadata()
	{
		return {"cornell.nppd.pack", 1};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellTensorState>> FArdaCornellNppdPackNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		return PrepareTensorShader<FArdaCornellNppdPackShader>(eastl::move(Device));
	}

	FArdaDependencyNodeDesc FArdaCornellNppdPackNode::Describe(const FArdaParameters&, const FArdaState& State)
	{
		return DescribeTensorShader(State);
	}

	FArdaRHIStatus FArdaCornellNppdPackNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return RecordTensorShader(Context, Parameters.mWidth, Parameters.mHeight);
	}

	FArdaDependencyNodeMetadata FArdaCornellTensorUnpackNode::GetMetadata()
	{
		return {"cornell.tensor.unpack", 1};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellTensorState>> FArdaCornellTensorPackNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		return PrepareTensorShader<FArdaCornellTensorPackShader>(eastl::move(Device));
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellTensorState>> FArdaCornellTensorUnpackNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		return PrepareTensorShader<FArdaCornellTensorUnpackShader>(eastl::move(Device));
	}

	FArdaDependencyNodeDesc FArdaCornellTensorPackNode::Describe(const FArdaParameters&, const FArdaState& State)
	{
		return DescribeTensorShader(State);
	}

	FArdaDependencyNodeDesc FArdaCornellTensorUnpackNode::Describe(const FArdaParameters&, const FArdaState& State)
	{
		return DescribeTensorShader(State);
	}

	FArdaRHIStatus FArdaCornellTensorPackNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return RecordTensorShader(Context, Parameters.mWidth, Parameters.mHeight);
	}

	FArdaRHIStatus FArdaCornellTensorUnpackNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return RecordTensorShader(Context, Parameters.mWidth, Parameters.mHeight);
	}
}
