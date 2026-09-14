#pragma once
#include <cstdint>

namespace arda
{
	/** Native counters wrap at the queue's valid bit width; intervals must be shorter than one wrap. */
	[[nodiscard]] constexpr uint64_t ArdaTimestampElapsedTicks(uint64_t Begin,
	    uint64_t End,
	    uint32_t ValidBits) noexcept
	{
		if (!ValidBits || ValidBits > 64)
		{
			return 0;
		}
		const uint64_t Mask = ValidBits == 64 ? UINT64_MAX : (uint64_t{1} << ValidBits) - 1;
		return (End - Begin) & Mask;
	}
}
