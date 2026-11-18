#pragma once
#include "RHI/Shaders/ArdaShaderType.h"
#include "ArdaTerrainNodeParameters.h"

namespace arda
{
	class FArdaTerrainOverlayPixelShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_TEXTURE_SRV(mSource, 0, 0, arda::EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTerrainOverlayPixelShader);
	};

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTerrainOverlayParameters)
		ARDA_NODE_SHADER_TEXTURE(mSource, FArdaTerrainOverlayPixelShader::FArdaParameters, mSource)
		ARDA_NODE_COLOR_TARGET(mColor)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mWidth, 0)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mHeight, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Self-contained operation; invocation registers and prepares this node's implementation. */
	class FArdaTerrainOverlayNode final
	    : public TArdaNodeDefinition<FArdaTerrainOverlayNode, FArdaTerrainOverlayParameters, EArdaNodeType::Graphics>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaTerrainOverlayNode, TerrainOverlay);
}
