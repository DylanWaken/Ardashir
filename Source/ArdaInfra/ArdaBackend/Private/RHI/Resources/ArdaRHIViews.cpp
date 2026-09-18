#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIViews.h"

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

		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}
	}

	size_t HashValue(const FArdaRHIViewDesc& V) noexcept
	{
		size_t H = 0;
		Combine(H, static_cast<uint8_t>(V.mFormat));
		Combine(H, static_cast<uint8_t>(V.mDimension));
		Combine(H, HashValue(V.mTextureRange));
		Combine(H, HashValue(V.mBufferRange));
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIViewDesc& V) noexcept
	{
		if (V.mTextureRange.mMipLevelCount == 0 || V.mTextureRange.mArraySliceCount == 0 ||
		    V.mBufferRange.mByteSize == 0)
		{
			return Invalid("View ranges must not be empty.");
		}
		return {};
	}
}
