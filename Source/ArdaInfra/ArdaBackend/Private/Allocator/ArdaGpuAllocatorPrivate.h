#pragma once

#include "Allocator/ArdaGpuAllocator.h"
#include "RHI/ArdaRHIProvider.h"

namespace arda
{
	/** Caches native objects behind leases retained by the facade and GPU submissions. */
	class FArdaGpuAllocator
	{
	public:
		explicit FArdaGpuAllocator(eastl::shared_ptr<IArdaRHIProviderDevice> Provider);
		~FArdaGpuAllocator();
		FArdaGpuAllocator(const FArdaGpuAllocator&) = delete;
		FArdaGpuAllocator& operator=(const FArdaGpuAllocator&) = delete;

		[[nodiscard]] FArdaProviderObjectResult CreateBuffer(const FArdaRHIBufferDesc& Desc);
		[[nodiscard]] FArdaProviderObjectResult CreateTexture(const FArdaRHITextureDesc& Desc);
		[[nodiscard]] FArdaProviderObjectResult CreateHeap(const FArdaRHIHeapDesc& Desc);
		[[nodiscard]] FArdaProviderObjectResult CreatePlacedBuffer(const FArdaRHIBufferDesc& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset);
		[[nodiscard]] FArdaProviderObjectResult CreatePlacedTexture(const FArdaRHITextureDesc& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset);
		[[nodiscard]] FArdaRHIStatus BindBufferMemory(FArdaProviderObjectRef& Object,
		    const FArdaRHIBufferDesc& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset);
		[[nodiscard]] FArdaRHIStatus BindTextureMemory(FArdaProviderObjectRef& Object,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset);

		void Collect(bool bTrim = false);
		[[nodiscard]] FArdaGpuAllocatorStats GetStats() const;
		[[nodiscard]] FArdaRHIStatus SetOptions(const FArdaGpuAllocatorOptions& Options);

	private:
		struct FArdaState;
		eastl::shared_ptr<FArdaState> mState;
	};
}
