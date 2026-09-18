/** Opaque imported CUDA memory, array and surface lifetime. */
#pragma once

#include "RHI/Resources/ArdaRHIResource.h"

namespace arda
{
	/** Owns imported external memory and all derived CUDA pointers/arrays/surfaces. */
	class IArdaCudaMapping
	{
	public:
		/** Releases CUDA views before imported memory; the native allocation must still be alive. */
		virtual ~IArdaCudaMapping() = default;

		/** Returns a buffer address plus Offset, or a surface handle at Mip; inputs were validated by the facade. */
		virtual uint64_t GetArgument(uint32_t Mip, uint64_t Offset) const = 0;
	};
}
