#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIComputePipeline.h"
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

	size_t HashValue(const FArdaRHIComputePipelineDesc& V) noexcept
	{
		size_t H = 0;
		HashPipelineRef(H, V.mComputeShader);
		HashPipelineRefs(H, V.mBindingLayouts);
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIComputePipelineDesc& V)
	{
		if (!V.mComputeShader)
		{
			return Invalid("Compute pipeline requires a compute shader.");
		}
		return V.mComputeShader->GetStage() == EArdaRHIShaderStage::Compute
		    ? FArdaRHIStatus{}
		    : Invalid("Compute pipeline shader has the wrong stage.");
	}
}
