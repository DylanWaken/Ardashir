#pragma once
#include "RHI/Shaders/ArdaShaderType.h"
#include "ArdaTerrainNodeParameters.h"

namespace arda
{
	class FArdaErodeTerrainShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_TEXTURE_SRV(mSource, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_TEXTURE_UAV(mHeightmap, 0, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaErodeTerrainShader);
	};

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTerrainErodeParameters)
		ARDA_NODE_SHADER_TEXTURE(mSource, FArdaErodeTerrainShader::FArdaParameters, mSource)
		ARDA_NODE_SHADER_TEXTURE(mHeightmap, FArdaErodeTerrainShader::FArdaParameters, mHeightmap)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Self-contained operation; invocation registers and prepares this node's implementation. */
	class FArdaTerrainErodeNode final
	    : public TArdaNodeDefinition<FArdaTerrainErodeNode, FArdaTerrainErodeParameters, EArdaNodeType::Compute>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaTerrainErodeNode, TerrainErode);
}
