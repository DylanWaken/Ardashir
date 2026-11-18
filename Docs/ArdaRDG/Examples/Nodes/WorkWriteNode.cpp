#include "WorkWriteNode.h"
#include "RecipeNodeSupport.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaWorkWriteNode, WorkWrite);

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaRecipeWorkShader,
	    ARDA_RDG_RECIPE_SHADER_DIR "/WorkWrite.hlsl",
	    "RecipeWork",
	    "RecipeEntry",
	    EArdaRHIShaderStage::WorkGraph)

	struct FArdaWorkWriteNode::FArdaState
	{
		FArdaGlobalShaderMap mShaders;
		FArdaInductorPipelineContribution mStage;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
	};

	FArdaRHIStatus FArdaWorkWriteNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = D.mStructureStride = sizeof(uint32_t);
		D.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
		return C.Buffer(P.mOutput, "Output", D);
	}

	FArdaDependencyNodeRequirements FArdaWorkWriteNode::GetRequirements(const FArdaParameters&)
	{
		FArdaDependencyNodeRequirements R;
		R.mFeatures.mbRequireWorkGraphs = true;
		return R;
	}

	FArdaDependencyNodeMetadata FArdaWorkWriteNode::GetMetadata()
	{
		return {"recipe.work-write", 1};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaWorkWriteNode::FArdaState>> FArdaWorkWriteNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Work-graph preparation needs a device.")};
		}

		auto State = eastl::make_shared<FArdaState>();
		if (!State->mShaders.Initialize(Device))
		{
			return {{}, ShaderError(State->mShaders)};
		}

		const auto* Shader = State->mShaders.Find(FArdaRecipeWorkShader::GetStaticType());
		if (!Shader)
		{
			return {{}, ShaderError(State->mShaders)};
		}
		State->mStage.mShader = Shader->GetShader();
		State->mStage.mBindingLayouts = Shader->GetBindingLayouts();
		auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		Config->mKind = EArdaPipelineStateKind::WorkGraph;
		Config->mWorkGraph.mDesc.mProgramName = "RecipeProgram";
		Config->mWorkGraph.mDesc.mEntryPoint = "RecipeEntry";
		Config->mWorkGraph.mDesc.mMaxInputRecords = 1;
		State->mConfiguration = eastl::move(Config);
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaWorkWriteNode::Describe(const FArdaParameters& P, const FArdaState& State)
	{
		FArdaDependencyNodeDesc D;
		D.mPipelineStages = {State.mStage};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::WorkGraph, {}, State.mConfiguration}};
		return D;
	}

	FArdaRHIStatus FArdaWorkWriteNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& State,
	    FArdaInstanceState&)
	{
		return C.DispatchWorkGraph(&P.mValue, 1, sizeof(P.mValue));
	}
}
