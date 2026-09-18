#pragma once

#include "RHI/Providers/ArdaRHIProvider.h"

namespace arda
{
	/** Constructs ArdaBackend's RHI facade around one backend-provider device. */
	[[nodiscard]] FArdaRHIDeviceRef CreateArdaRHIDevice(eastl::shared_ptr<IArdaRHIProviderDevice> Device);
}
