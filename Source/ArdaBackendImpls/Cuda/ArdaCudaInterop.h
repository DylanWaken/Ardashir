/** @file ArdaCudaInterop.h
 * Private D3D12/Vulkan CUDA lifetime bridge. Opaque contracts keep CUDA headers and driver
 * types out of both the public RHI and the CUDA-disabled native backend.
 */
#pragma once
#include "RHI/ArdaRHIProvider.h"

namespace arda
{
    /** Imported graphics synchronization primitive; retained until the CUDA stream completes. */
    class IArdaCudaSemaphore
    {
    public:
        virtual ~IArdaCudaSemaphore() = default;
        virtual void* GetNativeHandle() const = 0;
    };
    /** Owns imported external memory and all derived CUDA pointers/arrays/surfaces. */
    class IArdaCudaMapping
    {
    public:
        /** Releases CUDA views before imported memory; the native allocation must still be alive. */
        virtual ~IArdaCudaMapping() = default;
        /** Returns a buffer address plus Offset, or a surface handle at Mip; inputs were validated by the facade. */
        virtual uint64_t GetArgument(uint32_t Mip, uint64_t Offset) const = 0;
    };
    /** A single-use native capture or deferred ordinary-context batch, retained through fence retirement. */
    class IArdaCudaBatch : public IArdaProviderObject
    {
    public:
        /** Captures native launches, or retains validated ordinary launches without executing them. */
        virtual FArdaRHIStatus Record(void* CommandList,
            const eastl::vector<FArdaCudaKernel>& Kernels,
            const eastl::vector<uint64_t>& Bindings) = 0;
        /** Rejects a failed capture, replay, or violation of context capture/submission order. */
        virtual FArdaRHIStatus ValidateSubmit() const = 0;
        /** Advances the context's capture-order gate after native submission succeeds. */
        virtual void MarkSubmitted() = 0;
        /** Configures a GPU wait and signal before submission. Zero values select binary semaphore semantics. */
        virtual void SetSynchronization(eastl::shared_ptr<IArdaCudaSemaphore> Wait, uint64_t WaitValue,
            eastl::shared_ptr<IArdaCudaSemaphore> Signal, uint64_t SignalValue) = 0;
        /** Enqueues a deferred batch with configured GPU synchronization; unconfigured batches drain on the CPU. */
        virtual FArdaRHIStatus Execute() = 0;
    };
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
        virtual TArdaRHIResult<eastl::shared_ptr<IArdaCudaMapping>> ImportMemory(
            void* Handle, uint64_t AllocationSize, uint64_t BufferSize,
            const FArdaRHITextureDesc* Texture) = 0;
        /** Creates a retained, initially unrecorded single-use batch. */
        virtual eastl::shared_ptr<IArdaCudaBatch> CreateBatch() = 0;
    };
    /** LUID is eight bytes. Lifetime retains the native device and queue. No driver DLL
     * import is linked; missing drivers and CUDA-off builds return Unsupported. */
    TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaD3D12CudaContext(
        void* Queue, const void* Luid, eastl::shared_ptr<void> Lifetime,
        EArdaCudaExecutionMode Mode);
    /** Matches the Vulkan physical-device UUID and imports dedicated opaque OS handles. */
    TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaVulkanCudaContext(
        const void* DeviceUuid, void* ExternalQueueData, EArdaCudaExecutionMode Mode);
}
