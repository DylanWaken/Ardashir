#include "RHI/Pipelines/ArdaRHIWorkGraphPipeline.h"

#include "RHI/Pipelines/ArdaRHIPipelineHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{

	size_t HashValue(const FArdaRHIWorkGraphPipelineDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashString(H, V.mProgramName);
		ArdaHashString(H, V.mEntryPoint);
		HashPipelineRefs(H, V.mShaders);
		HashPipelineRefs(H, V.mGlobalBindingLayouts);
		ArdaHashCombine(H, V.mMaxInputRecords);
		return H;
	}
}
