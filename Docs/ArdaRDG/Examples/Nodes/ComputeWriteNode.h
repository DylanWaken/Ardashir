#pragma once
#include "RHI/Shaders/ArdaShaderType.h"
#include "Graph/Node/ArdaNodeDefinition.h"

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

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaComputeWriteParameters)
		ARDA_NODE_SHADER_BUFFER(mOutput, FArdaRecipeComputeShader::FArdaParameters, mOutput)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mValue, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaComputeWriteNode final
	    : public TArdaNodeDefinition<FArdaComputeWriteNode, FArdaComputeWriteParameters, EArdaNodeType::Compute>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaComputeWriteNode, ComputeWrite);
}
