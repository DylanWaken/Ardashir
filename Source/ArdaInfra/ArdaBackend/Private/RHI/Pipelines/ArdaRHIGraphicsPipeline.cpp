#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIFixedFunctionStates.h"
#include "RHI/Pipelines/ArdaRHIGraphicsPipeline.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include "RHI/Pipelines/ArdaRHIPipelineHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{
	namespace
	{

		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

	}

	size_t HashValue(const FArdaRHIGraphicsPipelineDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, static_cast<uint8_t>(V.mTopology));
		ArdaHashCombine(H, V.mPatchControlPoints);
		HashPipelineRef(H, V.mInputLayout);
		HashPipelineRef(H, V.mVertexShader);
		HashPipelineRef(H, V.mHullShader);
		HashPipelineRef(H, V.mDomainShader);
		HashPipelineRef(H, V.mGeometryShader);
		HashPipelineRef(H, V.mPixelShader);
		HashPipelineRefs(H, V.mBindingLayouts);
		CombineRasterFixedFunctionState(H, V);
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIGraphicsPipelineDesc& V)
	{
		if (!V.mVertexShader)
		{
			return Invalid("Graphics pipeline requires a vertex shader.");
		}
		if (V.mVertexShader->GetStage() != EArdaRHIShaderStage::Vertex)
		{
			return Invalid("Graphics pipeline vertex shader has the wrong stage.");
		}
		if (V.mHullShader && V.mHullShader->GetStage() != EArdaRHIShaderStage::Hull)
		{
			return Invalid("Graphics pipeline hull shader has the wrong stage.");
		}
		if (V.mDomainShader && V.mDomainShader->GetStage() != EArdaRHIShaderStage::Domain)
		{
			return Invalid("Graphics pipeline domain shader has the wrong stage.");
		}
		if (V.mGeometryShader && V.mGeometryShader->GetStage() != EArdaRHIShaderStage::Geometry)
		{
			return Invalid("Graphics pipeline geometry shader has the wrong stage.");
		}
		if (V.mPixelShader && V.mPixelShader->GetStage() != EArdaRHIShaderStage::Pixel)
		{
			return Invalid("Graphics pipeline pixel shader has the wrong stage.");
		}
		if (V.mSampleCount == 0 || V.mColorFormats.size() > ArdaRHIMaxRenderTargets)
		{
			return Invalid("Graphics pipeline sample count and attachment formats are invalid.");
		}
		if (V.mTopology == EArdaRHIPrimitiveTopology::PatchList && V.mPatchControlPoints == 0)
		{
			return Invalid("Patch-list pipelines require control points.");
		}
		return {};
	}
}
