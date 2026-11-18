#pragma once
#include "RHI/Shaders/ArdaShaderType.h"
#include "Graph/Node/ArdaNodeDefinition.h"

namespace arda
{
	class FArdaRecipeWorkShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_UAV(mOutput, 0, 0, EArdaRHIShaderStage::WorkGraph)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaRecipeWorkShader);
	};

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaWorkWriteParameters)
		ARDA_NODE_SHADER_BUFFER(mOutput, FArdaRecipeWorkShader::FArdaParameters, mOutput)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mValue, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaWorkWriteNode final
	    : public TArdaNodeDefinition<FArdaWorkWriteNode, FArdaWorkWriteParameters, EArdaNodeType::WorkGraph>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Hardware admission is checked before output declaration or device preparation. */
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaWorkWriteNode, WorkWrite);
}
