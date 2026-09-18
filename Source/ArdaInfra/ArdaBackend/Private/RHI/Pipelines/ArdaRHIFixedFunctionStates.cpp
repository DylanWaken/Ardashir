#include "RHI/Pipelines/ArdaRHIFixedFunctionStates.h"

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
	}

	size_t HashValue(const FArdaRHIRasterState& V) noexcept
	{
		size_t H = 0;
		Combine(H, static_cast<uint8_t>(V.mFillMode));
		Combine(H, static_cast<uint8_t>(V.mCullMode));
		Combine(H, V.mbFrontCounterClockwise);
		Combine(H, V.mbDepthClip);
		Combine(H, V.mbScissor);
		return H;
	}

	size_t HashValue(const FArdaRHIDepthStencilState& V) noexcept
	{
		size_t H = 0;
		Combine(H, V.mbDepthTest);
		Combine(H, V.mbDepthWrite);
		Combine(H, static_cast<uint8_t>(V.mDepthFunc));
		return H;
	}

	size_t HashValue(const FArdaRHIBlendTargetState& V) noexcept
	{
		size_t H = 0;
		Combine(H, V.mbEnable);
		Combine(H, static_cast<uint8_t>(V.mSourceColor));
		Combine(H, static_cast<uint8_t>(V.mDestinationColor));
		Combine(H, static_cast<uint8_t>(V.mSourceAlpha));
		Combine(H, static_cast<uint8_t>(V.mDestinationAlpha));
		return H;
	}

	size_t HashValue(const FArdaRHIBlendState& V) noexcept
	{
		size_t H = 0;
		Combine(H, V.mbAlphaToCoverage);
		for (const auto& Target : V.mTargets)
		{
			Combine(H, HashValue(Target));
		}
		return H;
	}
}
