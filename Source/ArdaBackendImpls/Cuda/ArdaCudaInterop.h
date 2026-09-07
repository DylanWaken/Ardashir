/** @file ArdaCudaInterop.h
 * Private D3D12/CUDA lifetime bridge. Opaque contracts keep CUDA headers and driver
 * types out of both the public RHI and the CUDA-disabled native backend.
 */
#pragma once
#include "RHI/ArdaRHIProvider.h"

namespace arda::backend::cuda
{
    using namespace rhi;
    using namespace rhi::provider;

    /** Owns imported external memory and all derived CUDA pointers/arrays/surfaces. */
    class IMapping
    {
    public:
        /** Releases CUDA views before imported memory; the native allocation must still be alive. */
        virtual ~IMapping() = default;
        /** Returns a buffer address plus Offset, or a surface handle at Mip; inputs were validated by the facade. */
        virtual uint64_t GetArgument(uint32_t Mip, uint64_t Offset) const = 0;
    };
    /** One single-use CiG capture retained with its command list through GPU fence retirement. */
    class IBatch : public IArdaProviderObject
    {
    public:
        /** Captures launches into the supplied native list; failure can poison the recording. */
        virtual FArdaRHIStatus Record(void* CommandList,
            const eastl::vector<FArdaCudaKernel>& Kernels,
            const eastl::vector<uint64_t>& Bindings) = 0;
        /** Rejects a failed capture, replay, or violation of context capture/submission order. */
        virtual FArdaRHIStatus ValidateSubmit() const = 0;
        /** Advances the context's capture-order gate after native submission succeeds. */
        virtual void MarkSubmitted() = 0;
    };
    /** Owns the matched CUDA context, module cache, and retained D3D12 device/queue lifetime. */
    class IContext
    {
    public:
        /** Destroys cached modules before the CUDA context and native lifetime owner. */
        virtual ~IContext() = default;
        /** Returns qualified device limits; surface support may be independently unavailable. */
        virtual FArdaCudaCapabilities GetCapabilities() const = 0;
        /** Imports a borrowed NT handle. Texture selects an array mapping; null selects BufferSize bytes. */
        virtual TArdaRHIResult<eastl::shared_ptr<IMapping>> ImportMemory(
            void* Handle, uint64_t AllocationSize, uint64_t BufferSize,
            const FArdaRHITextureDesc* Texture) = 0;
        /** Creates a retained, initially unrecorded single-use capture batch. */
        virtual eastl::shared_ptr<IBatch> CreateBatch() = 0;
    };
    /** LUID is eight bytes. Lifetime retains the native device and queue. No driver DLL
     * import is linked; missing drivers and CUDA-off builds return Unsupported. */
    TArdaRHIResult<eastl::shared_ptr<IContext>> CreateD3D12Context(
        void* Queue, const void* Luid, eastl::shared_ptr<void> Lifetime);
}
