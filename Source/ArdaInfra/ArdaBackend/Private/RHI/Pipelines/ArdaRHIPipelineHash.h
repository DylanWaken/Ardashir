/** In-process hashing for references and fixed-function state shared by pipeline descriptors. */
#pragma once
#include "RHI/Resources/ArdaHash.h"
#include "RHI/Pipelines/ArdaRHIFixedFunctionStates.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include <EASTL/vector.h>

namespace arda
{
	template <typename T>
	void HashPipelineRef(size_t& Seed, const TArdaRHIRef<T>& Value) noexcept
	{
		ArdaHashCombine(Seed, reinterpret_cast<uintptr_t>(Value.Get()));
	}

	template <typename T>
	void HashPipelineRefs(size_t& Seed, const eastl::vector<TArdaRHIRef<T>>& Values) noexcept
	{
		ArdaHashCombine(Seed, Values.size());
		for (const auto& Value : Values)
		{
			HashPipelineRef(Seed, Value);
		}
	}

	template <typename PipelineDesc>
	void CombineRasterFixedFunctionState(size_t& Hash, const PipelineDesc& Value)
	{
		ArdaHashCombine(Hash, HashValue(Value.mBlendState));
		ArdaHashCombine(Hash, HashValue(Value.mRasterState));
		ArdaHashCombine(Hash, HashValue(Value.mDepthStencilState));
		for (auto Format : Value.mColorFormats)
		{
			ArdaHashCombine(Hash, static_cast<uint8_t>(Format));
		}
		ArdaHashCombine(Hash, static_cast<uint8_t>(Value.mDepthFormat));
		ArdaHashCombine(Hash, Value.mSampleCount);
	}
}
