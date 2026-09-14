#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellAccumulateNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	class FArdaAccumulateCornellSamplesShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mSampleRadiance, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_TEXTURE_UAV(mAccumulation, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_UNIFORM_BUFFER(mFrame, 0, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaAccumulateCornellSamplesShader);
	};

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

	eastl::string FArdaCornellAccumulateNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mRadiance);
		Key.Resource(P.mAccumulation);
		Key.Resource(P.mConstants);
		Key.Value(P.mGroupCountX);
		Key.Value(P.mGroupCountY);
		Key.Value(P.mWorkspaceBytes);
		return Key.Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellAccumulateNode::FArdaState>> FArdaCornellAccumulateNode::Prepare(
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

	FArdaDependencyNodeDesc FArdaCornellAccumulateNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		D.BindShader<FArdaAccumulateCornellSamplesShader::FArdaParameters>({{"mSampleRadiance", P.mRadiance},
		    {"mAccumulation", P.mAccumulation, 0, EArdaDependencyAccess::ReadWrite},
		    {"mFrame", P.mConstants}});

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
