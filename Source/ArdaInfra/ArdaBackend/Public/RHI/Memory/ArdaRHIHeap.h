/** @file ArdaRHIHeap.h
 * Declares Heap definitions for the RHI memory module.
 */

#pragma once

#include "RHI/Memory/ArdaRHIMemoryTypes.h"
#include "RHI/Resources/ArdaRHIResource.h"

#include <EASTL/string.h>
#include <cstdint>

namespace arda
{
	/** Describes heap desc. */
	struct FArdaRHIHeapDesc
	{
		/** Stores the capacity. */
		uint64_t mCapacity = 0;
		/** Stores the type. */
		EArdaRHIHeapType mType = EArdaRHIHeapType::DeviceLocal;
		/** Compatible backend memory types, normally copied/intersected from requirements. */
		uint32_t mMemoryTypeBits = 0xffffffffu;
		/** Stores the debug name. */
		eastl::string mDebugName;
	};

	/** Interface for heap. */
	class IArdaRHIHeap : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIHeapDesc& GetDesc() const noexcept = 0;
	};
}
