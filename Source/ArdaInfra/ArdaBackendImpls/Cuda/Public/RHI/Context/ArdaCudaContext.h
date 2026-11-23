/** Matched CUDA device context, limits and imported native lifetimes. */
#pragma once

#include "RHI/Scheduling/ArdaCudaBatch.h"
#include "RHI/Interop/ArdaCudaMapping.h"

namespace arda
{
	/** Owns the matched CUDA context, entry-limit cache, and any required native lifetime token. */
	class IArdaCudaContext
	{
	public:
		/** Destroys cached entries before the CUDA context and native lifetime owner. */
		virtual ~IArdaCudaContext() = default;

		/** Returns qualified device limits; surface support may be independently unavailable. */
		virtual FArdaCudaCapabilities GetCapabilities() const = 0;

		/** Imports a borrowed D3D12 fence or Vulkan binary semaphore OS handle. Consumes Vulkan FDs. */
		virtual TArdaRHIResult<eastl::shared_ptr<IArdaCudaSemaphore>> ImportSemaphore(void* Handle) = 0;

		/** Imports a borrowed NT handle or consumes an opaque Vulkan FD (including on failure).
         * Texture selects an array mapping; null selects BufferSize bytes. */
		virtual TArdaRHIResult<eastl::shared_ptr<IArdaCudaMapping>> ImportMemory(void* Handle,
		    uint64_t AllocationSize,
		    uint64_t BufferSize,
		    const FArdaRHITextureDesc* Texture) = 0;

		/** Creates a retained, initially unrecorded single-use batch. */
		virtual eastl::shared_ptr<IArdaCudaBatch> CreateBatch() = 0;
	};
}
