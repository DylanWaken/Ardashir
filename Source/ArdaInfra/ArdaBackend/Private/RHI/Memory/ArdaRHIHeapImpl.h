/** Native GPU heap facade resource. */
#pragma once

#include "RHI/Memory/ArdaRHIHeap.h"
#include "RHI/Resources/ArdaRHIResourceImpl.h"

namespace arda::detail
{
	using FArdaHeap = TArdaNativeResource<IArdaRHIHeap, FArdaRHIHeapDesc, EArdaRHIResourceType::Heap>;
}
