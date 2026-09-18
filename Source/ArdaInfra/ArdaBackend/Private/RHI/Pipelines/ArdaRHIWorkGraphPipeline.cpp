#include "RHI/Pipelines/ArdaRHIWorkGraphPipeline.h"

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
	}

	size_t HashValue(const FArdaRHIWorkGraphPipelineDesc& V) noexcept
	{
		size_t H = 0;
		CombineString(H, V.mProgramName);
		CombineString(H, V.mEntryPoint);
		CombineRefs(H, V.mShaders);
		CombineRefs(H, V.mGlobalBindingLayouts);
		Combine(H, V.mMaxInputRecords);
		return H;
	}
}
