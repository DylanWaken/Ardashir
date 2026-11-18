#pragma once
#include "RHI/Shaders/ArdaShaderType.h"
#include "ArdaTerrainNodeParameters.h"

namespace arda
{
	class FArdaTerrainVertexShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_CONSTANT_BUFFER(mCamera, 0, 0, arda::EArdaRHIShaderStage::Vertex)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTerrainVertexShader);
	};

	class FArdaTerrainPixelShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_TEXTURE_SRV(mHeightmap, 0, 0, arda::EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTerrainPixelShader);
	};

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTerrainDrawParameters)
		ARDA_NODE_SHADER_TEXTURE(mHeightmap, FArdaTerrainPixelShader::FArdaParameters, mHeightmap)
		ARDA_NODE_BUFFER(mVertices, EArdaDependencyAccess::Read, EArdaRHIResourceState::VertexBuffer)
		ARDA_NODE_BUFFER(mIndices, EArdaDependencyAccess::Read, EArdaRHIResourceState::IndexBuffer)
		ARDA_NODE_SHADER_BUFFER(mCamera, FArdaTerrainVertexShader::FArdaParameters, mCamera)
		ARDA_NODE_COLOR_TARGET(mColor)
		ARDA_NODE_DEPTH_TARGET(mDepth, EArdaDependencyAccess::Write)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mWidth, 0)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mHeight, 0)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(EArdaRHIFormat, mColorFormat, EArdaRHIFormat::RGBA8UNorm)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Self-contained operation; invocation registers and prepares this node's implementation. */
	class FArdaTerrainDrawNode final
	    : public TArdaNodeDefinition<FArdaTerrainDrawNode, FArdaTerrainDrawParameters, EArdaNodeType::Graphics>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaTerrainDrawNode, TerrainDraw);
}
