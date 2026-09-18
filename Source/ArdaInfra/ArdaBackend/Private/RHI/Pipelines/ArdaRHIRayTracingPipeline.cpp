#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIRayTracingPipeline.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include "RHI/Resources/ArdaHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{
	namespace
	{
		template <typename T>
		void Combine(size_t& Seed, const T& Value) noexcept
		{
			ArdaHashCombine(Seed, Value);
		}

		void CombineString(size_t& Seed, const eastl::string& Value) noexcept
		{
			ArdaHashString(Seed, Value);
		}

		template <typename T>
		void CombineRef(size_t& Seed, const TArdaRHIRef<T>& Value) noexcept
		{
			Combine(Seed, reinterpret_cast<uintptr_t>(Value.Get()));
		}

		template <typename T>
		void CombineRefs(size_t& Seed, const eastl::vector<TArdaRHIRef<T>>& Values) noexcept
		{
			Combine(Seed, Values.size());
			for (const auto& Value : Values)
			{
				CombineRef(Seed, Value);
			}
		}

		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}
	}

	size_t HashValue(const FArdaRHIRayTracingPipelineShaderDesc& V) noexcept
	{
		size_t H = 0;
		CombineString(H, V.mExportName);
		CombineRef(H, V.mShader);
		CombineRef(H, V.mLocalBindingLayout);
		return H;
	}

	size_t HashValue(const FArdaRHIRayTracingHitGroupDesc& V) noexcept
	{
		size_t H = 0;
		CombineString(H, V.mExportName);
		CombineRef(H, V.mClosestHitShader);
		CombineRef(H, V.mAnyHitShader);
		CombineRef(H, V.mIntersectionShader);
		CombineRef(H, V.mLocalBindingLayout);
		Combine(H, V.mbProceduralPrimitive);
		return H;
	}

	size_t HashValue(const FArdaRHIRayTracingPipelineDesc& V) noexcept
	{
		size_t H = 0;
		Combine(H, V.mShaders.size());
		for (const auto& S : V.mShaders)
		{
			Combine(H, HashValue(S));
		}
		Combine(H, V.mHitGroups.size());
		for (const auto& G : V.mHitGroups)
		{
			Combine(H, HashValue(G));
		}
		CombineRefs(H, V.mGlobalBindingLayouts);
		Combine(H, V.mMaxPayloadSize);
		Combine(H, V.mMaxAttributeSize);
		Combine(H, V.mMaxRecursionDepth);
		Combine(H, V.mbAllowOpacityMicromaps);
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
