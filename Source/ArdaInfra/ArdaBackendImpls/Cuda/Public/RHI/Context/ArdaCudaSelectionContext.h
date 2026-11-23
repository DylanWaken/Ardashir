/** Host metadata used to select a compatible CUDA kernel variant. */
#pragma once

#include "RHI/Config/ArdaCudaConfig.h"
#include "RHI/Scheduling/ArdaRHIQueueTypes.h"

namespace arda
{
	/** Host metadata available during selection; streams and addresses stay in the provider. */
	struct FArdaCudaSelectionContext
	{
		FArdaCudaCapabilities mCapabilities;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
	};
}
