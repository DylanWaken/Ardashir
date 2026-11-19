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
		if (!V.mExtension.mId.empty())
		{
			ArdaHashCombine(H, eastl::hash<eastl::string>{}(V.mExtension.mId));
			for (const auto Word : V.mExtension.mArguments)
			{
				ArdaHashCombine(H, Word);
			}
		}
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIComputePipelineDesc& V)
	{
		if (V.mExtension.mId.empty() && !V.mExtension.mArguments.empty())
		{
			return Invalid("Compute pipeline extension arguments require an extension ID.");
		}
		if (!V.mComputeShader)
		{
			return Invalid("Compute pipeline requires a compute shader.");
		}
		return V.mComputeShader->GetStage() == EArdaRHIShaderStage::Compute
		    ? FArdaRHIStatus{}
		    : Invalid("Compute pipeline shader has the wrong stage.");
	}
}
