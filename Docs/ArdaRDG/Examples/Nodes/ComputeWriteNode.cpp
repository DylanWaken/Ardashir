#include "ComputeWriteNode.h"
#include "RecipeNodeSupport.h"
#include <cstring>

namespace arda
{
	class FArdaRecipeComputeShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_PUSH_CONSTANTS(uint32_t, mValue, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mOutput, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaRecipeComputeShader);
	};
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaRecipeComputeShader,
	    ARDA_RDG_RECIPE_SHADER_DIR "/ComputeWrite.hlsl",
	    "RecipeCompute",
	    "RecipeCS",
	    EArdaRHIShaderStage::Compute)

	struct FArdaComputeWriteNode::FArdaState
	{
		FArdaGlobalShaderMap mShaders;
		FArdaInductorPipelineContribution mStage;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
	};

	FArdaRHIStatus FArdaComputeWriteNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = D.mStructureStride = sizeof(uint32_t);
		D.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
		return C.Buffer(P.mOutput, "Output", D);
	}

	FArdaDependencyNodeMetadata FArdaComputeWriteNode::GetMetadata()
	{
		return {"recipe.compute-write", 1};
	}

	eastl::string FArdaComputeWriteNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mOutput);
		Key.Value(P.mValue); // CPU input record is frozen with this instance.
		return Key.Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaComputeWriteNode::FArdaState>> FArdaComputeWriteNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Compute preparation needs a device.")};
		}

		auto State = eastl::make_shared<FArdaState>();
		if (!State->mShaders.Initialize(Device))
		{
			return {{}, ShaderError(State->mShaders)};
		}

		const auto* Shader = State->mShaders.Find(FArdaRecipeComputeShader::GetStaticType());
		if (!Shader)
		{
			return {{}, ShaderError(State->mShaders)};
		}
		State->mStage.mShader = Shader->GetShader();
		State->mStage.mBindingLayouts = Shader->GetBindingLayouts();
		auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		Config->mKind = EArdaPipelineStateKind::Compute;
		State->mConfiguration = eastl::move(Config);
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaComputeWriteNode::Describe(const FArdaParameters& P, const FArdaState& State)
	{
		FArdaDependencyNodeDesc D;
		FArdaDependencyShaderValue Value;
		Value.mMember = "mValue";
		Value.mBytes.resize(sizeof(P.mValue));
		std::memcpy(Value.mBytes.data(), &P.mValue, sizeof(P.mValue));
		D.BindShader<FArdaRecipeComputeShader::FArdaParameters>({{"mOutput", P.mOutput}}, "default", {Value});
		D.mPipelineStages = {State.mStage};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, State.mConfiguration}};
		return D;
	}

	FArdaRHIStatus FArdaComputeWriteNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& State,
	    FArdaInstanceState&)
	{
		// Bind the executor-prepared pipeline and shader arguments before dispatching this operation.
		if (auto S = C.SetComputeState(); !S)
		{
			return S;
		}

		C.GetCommands().Dispatch(1, 1, 1);
		return {};
	}
}
