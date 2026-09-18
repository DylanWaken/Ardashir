#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIComputePipeline.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include "ArdaHash.h"

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

	size_t HashValue(const FArdaRHIComputePipelineDesc& V) noexcept
	{
		size_t H = 0;
		CombineRef(H, V.mComputeShader);
		CombineRefs(H, V.mBindingLayouts);
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
