#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIRayTracingPipeline.h"
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

	size_t HashValue(const FArdaRHIRayTracingPipelineShaderDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashString(H, V.mExportName);
		HashPipelineRef(H, V.mShader);
		HashPipelineRef(H, V.mLocalBindingLayout);
		return H;
	}

	size_t HashValue(const FArdaRHIRayTracingHitGroupDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashString(H, V.mExportName);
		HashPipelineRef(H, V.mClosestHitShader);
		HashPipelineRef(H, V.mAnyHitShader);
		HashPipelineRef(H, V.mIntersectionShader);
		HashPipelineRef(H, V.mLocalBindingLayout);
		ArdaHashCombine(H, V.mbProceduralPrimitive);
		return H;
	}

	size_t HashValue(const FArdaRHIRayTracingPipelineDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, V.mShaders.size());
		for (const auto& S : V.mShaders)
		{
			ArdaHashCombine(H, HashValue(S));
		}
		ArdaHashCombine(H, V.mHitGroups.size());
		for (const auto& G : V.mHitGroups)
		{
			ArdaHashCombine(H, HashValue(G));
		}
		HashPipelineRefs(H, V.mGlobalBindingLayouts);
		ArdaHashCombine(H, V.mMaxPayloadSize);
		ArdaHashCombine(H, V.mMaxAttributeSize);
		ArdaHashCombine(H, V.mMaxRecursionDepth);
		ArdaHashCombine(H, V.mbAllowOpacityMicromaps);
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIRayTracingPipelineDesc& V)
	{
		if (V.mShaders.empty() || V.mMaxRecursionDepth == 0)
		{
			return Invalid("Ray-tracing pipelines require shaders and non-zero recursion depth.");
		}
		for (const auto& S : V.mShaders)
		{
			if (S.mExportName.empty() || !S.mShader ||
			    !HasAnyFlags(EArdaRHIShaderStage::AllRayTracing, S.mShader->GetStage()))
			{
				return Invalid("Ray-tracing pipeline shader exports require a name and ray-tracing shader.");
			}
		}
		for (const auto& H : V.mHitGroups)
		{
			if (H.mExportName.empty() || (!H.mClosestHitShader && !H.mAnyHitShader && !H.mIntersectionShader))
			{
				return Invalid("Ray-tracing hit groups require a name and at least one shader.");
			}
			if (H.mbProceduralPrimitive && !H.mIntersectionShader)
			{
				return Invalid("Procedural ray-tracing hit groups require an intersection shader.");
			}
		}
		return {};
	}
}
