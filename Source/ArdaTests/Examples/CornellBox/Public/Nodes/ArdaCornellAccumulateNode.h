#pragma once
#include "RHI/Shaders/ArdaShaderType.h"
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	class FArdaAccumulateCornellSamplesShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mSampleRadiance, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_TEXTURE_UAV_ACCESS(mAccumulation,
			    0,
			    0,
			    arda::EArdaRHIShaderStage::Compute,
			    EArdaShaderResourceAccess::ReadWrite)
			ARDA_SHADER_UNIFORM_BUFFER(mFrame, 0, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaAccumulateCornellSamplesShader);
	};

	/** Parameters owned by one Cornell Accumulate node instance. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCornellAccumulateParameters)
		/** Input sample radiance buffer. */
		ARDA_NODE_SHADER_BUFFER(mRadiance, FArdaAccumulateCornellSamplesShader::FArdaParameters, mSampleRadiance)
		/** Accumulation texture updated by this node. */
		ARDA_NODE_SHADER_TEXTURE(mAccumulation, FArdaAccumulateCornellSamplesShader::FArdaParameters, mAccumulation)
		/** Input frame constant buffer. */
		ARDA_NODE_SHADER_BUFFER(mConstants, FArdaAccumulateCornellSamplesShader::FArdaParameters, mFrame)
		/** Number of accumulation compute groups along X. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mGroupCountX, 1)
		/** Number of accumulation compute groups along Y. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mGroupCountY, 1)
		/** Scratch memory reserved while recording this node. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mWorkspaceBytes, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaCornellAccumulateNode final : public TArdaNodeDefinition<FArdaCornellAccumulateNode,
	                                             FArdaCornellAccumulateParameters,
	                                             EArdaNodeType::Compute>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaCornellAccumulateNode, CornellAccumulate);
}
