#pragma once

#include "RHI/Shaders/ArdaShaderType.h"
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	class FArdaCornellNppdPackShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mRadiance, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_SRV(mGuides, 1, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mColor, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mDepth, 1, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mNormal, 2, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mDiffuse, 3, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mMotion, 4, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mMeanColor, 5, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_UNIFORM_BUFFER(mFrame, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellNppdPackShader);
	};

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCornellNppdPackParameters)
		ARDA_NODE_SHADER_BUFFER(mRadiance, FArdaCornellNppdPackShader::FArdaParameters, mRadiance)
		ARDA_NODE_SHADER_BUFFER(mGuides, FArdaCornellNppdPackShader::FArdaParameters, mGuides)
		ARDA_NODE_SHADER_BUFFER(mColor, FArdaCornellNppdPackShader::FArdaParameters, mColor)
		ARDA_NODE_SHADER_BUFFER(mDepth, FArdaCornellNppdPackShader::FArdaParameters, mDepth)
		ARDA_NODE_SHADER_BUFFER(mNormal, FArdaCornellNppdPackShader::FArdaParameters, mNormal)
		ARDA_NODE_SHADER_BUFFER(mDiffuse, FArdaCornellNppdPackShader::FArdaParameters, mDiffuse)
		ARDA_NODE_SHADER_BUFFER(mMotion, FArdaCornellNppdPackShader::FArdaParameters, mMotion)
		ARDA_NODE_SHADER_BUFFER(mMeanColor, FArdaCornellNppdPackShader::FArdaParameters, mMeanColor)
		ARDA_NODE_SHADER_BUFFER(mConstants, FArdaCornellNppdPackShader::FArdaParameters, mFrame)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mWidth, 1)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mHeight, 1)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaCornellTensorPackShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_TEXTURE_SRV(mImage, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mTensor, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_UNIFORM_BUFFER(mFrame, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellTensorPackShader);
	};

	class FArdaCornellTensorUnpackShader final : public FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mTensor, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_TEXTURE_UAV(mImage, 0, 0, EArdaRHIShaderStage::Compute)
			ARDA_SHADER_UNIFORM_BUFFER(mFrame, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellTensorUnpackShader);
	};

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCornellTensorPackParameters)
		ARDA_NODE_SHADER_TEXTURE(mImage, FArdaCornellTensorPackShader::FArdaParameters, mImage)
		ARDA_NODE_SHADER_BUFFER(mTensor, FArdaCornellTensorPackShader::FArdaParameters, mTensor)
		ARDA_NODE_SHADER_BUFFER(mConstants, FArdaCornellTensorPackShader::FArdaParameters, mFrame)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mWidth, 1)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mHeight, 1)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCornellTensorUnpackParameters)
		ARDA_NODE_SHADER_BUFFER(mTensor, FArdaCornellTensorUnpackShader::FArdaParameters, mTensor)
		ARDA_NODE_SHADER_TEXTURE(mImage, FArdaCornellTensorUnpackShader::FArdaParameters, mImage)
		ARDA_NODE_SHADER_BUFFER(mConstants, FArdaCornellTensorUnpackShader::FArdaParameters, mFrame)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mWidth, 1)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mHeight, 1)
	ARDA_END_NODE_PARAMETER_STRUCT()

	struct FArdaCornellTensorState;

	class FArdaCornellNppdPackNode final
	    : public TArdaNodeDefinition<FArdaCornellNppdPackNode, FArdaCornellNppdPackParameters, EArdaNodeType::Compute>
	{
	public:
		using FArdaState = FArdaCornellTensorState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&);
	};

	class FArdaCornellTensorPackNode final : public TArdaNodeDefinition<FArdaCornellTensorPackNode,
	                                             FArdaCornellTensorPackParameters,
	                                             EArdaNodeType::Compute>
	{
	public:
		using FArdaState = FArdaCornellTensorState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&);
	};

	class FArdaCornellTensorUnpackNode final : public TArdaNodeDefinition<FArdaCornellTensorUnpackNode,
	                                               FArdaCornellTensorUnpackParameters,
	                                               EArdaNodeType::Compute>
	{
	public:
		using FArdaState = FArdaCornellTensorState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCornellTensorPackNode, CornellTensorPack);
	ARDA_DECLARE_GRAPH_NODE(FArdaCornellTensorUnpackNode, CornellTensorUnpack);
	ARDA_DECLARE_GRAPH_NODE(FArdaCornellNppdPackNode, CornellNppdPack);
}
