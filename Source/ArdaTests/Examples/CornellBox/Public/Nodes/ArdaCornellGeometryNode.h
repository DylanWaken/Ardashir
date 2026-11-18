#pragma once
#include "RHI/Shaders/ArdaShaderType.h"
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	class FArdaGenerateCornellGeometryShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_UAV(mVertices, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mIndices, 1, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mMaterials, 2, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaGenerateCornellGeometryShader);
	};

	/** Parameters owned by one Cornell Geometry node instance. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCornellGeometryParameters)
		/** Output vertex buffer. */
		ARDA_NODE_SHADER_BUFFER(mVertices, FArdaGenerateCornellGeometryShader::FArdaParameters, mVertices)
		/** Output index buffer. */
		ARDA_NODE_SHADER_BUFFER(mIndices, FArdaGenerateCornellGeometryShader::FArdaParameters, mIndices)
		/** Output material buffer. */
		ARDA_NODE_SHADER_BUFFER(mMaterials, FArdaGenerateCornellGeometryShader::FArdaParameters, mMaterials)
		/** Number of geometry compute groups along X. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mGroupCountX, 1)
		/** Number of geometry compute groups along Y. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mGroupCountY, 1)
		/** Scratch memory reserved while recording this node. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mWorkspaceBytes, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaCornellGeometryNode final
	    : public TArdaNodeDefinition<FArdaCornellGeometryNode, FArdaCornellGeometryParameters, EArdaNodeType::Compute>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaCornellGeometryNode, CornellGeometry);
}
