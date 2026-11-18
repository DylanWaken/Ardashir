#pragma once
#include "RHI/Shaders/ArdaShaderType.h"
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	class FArdaCornellPresentPixelShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_TEXTURE_SRV(mAccumulation, 0, 0, arda::EArdaRHIShaderStage::Pixel)
			ARDA_SHADER_UNIFORM_BUFFER(mFrame, 0, 0, arda::EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellPresentPixelShader);
	};

	/** Parameters owned by one Cornell Present node instance. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCornellPresentParameters)
		/** Input accumulated radiance texture. */
		ARDA_NODE_SHADER_TEXTURE(mAccumulation, FArdaCornellPresentPixelShader::FArdaParameters, mAccumulation)
		/** Input frame constant buffer. */
		ARDA_NODE_SHADER_BUFFER(mConstants, FArdaCornellPresentPixelShader::FArdaParameters, mFrame)
		/** Output render-target texture. */
		ARDA_NODE_COLOR_TARGET(mColor)
		/** Viewport and scissor width in pixels. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mWidth, 1)
		/** Viewport and scissor height in pixels. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mHeight, 1)
		/** Scratch memory reserved while recording this node. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mWorkspaceBytes, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaCornellPresentNode final
	    : public TArdaNodeDefinition<FArdaCornellPresentNode, FArdaCornellPresentParameters, EArdaNodeType::Graphics>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaCornellPresentNode, CornellPresent);
}
