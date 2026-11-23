/** Native backend entry points for creating matched CUDA execution contexts. */
#pragma once

#include "RHI/Context/ArdaCudaContext.h"

namespace arda
{
	/** LUID is eight bytes. Lifetime retains the native device and queue. No driver DLL
     * import is linked; missing drivers and CUDA-off builds return Unsupported. */
	TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaD3D12CudaContext(void* Queue,
	    const void* Luid,
	    eastl::shared_ptr<void> Lifetime,
	    EArdaCudaExecutionMode Mode);

	/** Matches the Vulkan physical-device UUID and imports dedicated opaque OS handles. */
	TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaVulkanCudaContext(const void* DeviceUuid,
	    void* ExternalQueueData,
	    EArdaCudaExecutionMode Mode);
}
