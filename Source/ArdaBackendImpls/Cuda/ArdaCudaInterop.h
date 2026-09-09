/** @file ArdaCudaInterop.h
 * Private D3D12/Vulkan CUDA lifetime bridge. Opaque contracts keep CUDA headers and driver
 * types out of both the public RHI and the CUDA-disabled native backend.
 */
#pragma once
#include "RHI/ArdaRHIProvider.h"

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
        /** Executes a deferred batch after graphics completion and waits for its CUDA stream. */
        virtual FArdaRHIStatus Execute() = 0;
    };
    /** Owns the matched CUDA context, module cache, and any required native lifetime token. */
    class IArdaCudaContext
    {
    public:
        /** Destroys cached modules before the CUDA context and native lifetime owner. */
        virtual ~IArdaCudaContext() = default;
        /** Returns qualified device limits; surface support may be independently unavailable. */
        virtual FArdaCudaCapabilities GetCapabilities() const = 0;
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
    TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaVulkanCudaContext(const void* DeviceUuid);
}
