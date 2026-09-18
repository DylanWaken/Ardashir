/** @file ArdaRHIQueueTypes.h
 * Declares QueueTypes definitions for the RHI scheduling module.
 */

#pragma once


#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Enumerates queue type values. */
	enum class EArdaRHIQueueType : uint8_t
	{
		Graphics = 0,
		Compute = 1,
		Copy = 2,
		Count
	};

	/** Number of queue types represented by EArdaRHIQueueType. */
	inline constexpr size_t ArdaRHIQueueTypeCount = static_cast<size_t>(EArdaRHIQueueType::Count);

	/** @return The canonical array index for a queue type. */
	[[nodiscard]] inline constexpr size_t GetArdaRHIQueueIndex(EArdaRHIQueueType Queue) noexcept
	{
		return static_cast<size_t>(Queue);
	}
}
