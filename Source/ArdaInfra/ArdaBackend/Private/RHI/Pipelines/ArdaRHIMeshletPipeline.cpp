#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIFixedFunctionStates.h"
#include "RHI/Pipelines/ArdaRHIMeshletPipeline.h"
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

	size_t HashValue(const FArdaRHIMeshletPipelineDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, static_cast<uint8_t>(V.mTopology));
		HashPipelineRef(H, V.mAmplificationShader);
		HashPipelineRef(H, V.mMeshShader);
		HashPipelineRef(H, V.mPixelShader);
		HashPipelineRefs(H, V.mBindingLayouts);
		CombineRasterFixedFunctionState(H, V);
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIMeshletPipelineDesc& V)
	{
		if (!V.mMeshShader)
		{
			return Invalid("Meshlet pipeline requires a mesh shader.");
		}
		if (V.mMeshShader->GetStage() != EArdaRHIShaderStage::Mesh)
		{
			return Invalid("Meshlet pipeline mesh shader has the wrong stage.");
		}
		if (V.mAmplificationShader && V.mAmplificationShader->GetStage() != EArdaRHIShaderStage::Amplification)
		{
			return Invalid("Meshlet pipeline amplification shader has the wrong stage.");
		}
		if (V.mPixelShader && V.mPixelShader->GetStage() != EArdaRHIShaderStage::Pixel)
		{
			return Invalid("Meshlet pipeline pixel shader has the wrong stage.");
		}
		if (V.mSampleCount == 0 || V.mColorFormats.size() > ArdaRHIMaxRenderTargets)
		{
			return Invalid("Meshlet pipeline sample count and attachment formats are invalid.");
		}
		return {};
	}
}
