/** @file ArdaRHIMemoryTypes.h
 * Declares MemoryTypes definitions for the RHI memory module.
 */

#pragma once


#include <cstdint>

namespace arda
{
	/** Physical allocation retained by a resource. Placed objects identify their entire parent heap. */
	struct FArdaRHIMemoryAllocationInfo
	{
		/** Stable within the device while the resource remains alive; shared allocations use one identity. */
		const void* mIdentity = nullptr;
		/** Retained allocation capacity, including native padding and unused heap ranges. */
		uint64_t mByteSize = 0;
		/** False when an externally imported handle does not reveal its allocation. */
		bool mbKnown = false;

		bool operator==(const FArdaRHIMemoryAllocationInfo& Other) const noexcept
		{
			return mIdentity == Other.mIdentity && mByteSize == Other.mByteSize && mbKnown == Other.mbKnown;
		}
	};

	/** Enumerates CPU access values. */
	enum class EArdaRHICpuAccess : uint8_t
	{
		None,
		Read,
		Write
	};

	/** Enumerates heap type values. */
	enum class EArdaRHIHeapType : uint8_t
	{
		DeviceLocal,
		Upload,
		Readback
	};

	/** Describes memory requirements. */
	struct FArdaRHIMemoryRequirements
	{
		/** Stores the size. */
		uint64_t mSize = 0;
		/** Stores the alignment. */
		uint64_t mAlignment = 0;
		/** Backend memory-type compatibility mask (all bits for APIs without memory types). */
		uint32_t mMemoryTypeBits = 0xffffffffu;
	};
}
