/** @file ArdaRHICuda.h
 * Describes CUDA launches recorded into an RHI command list. Public declarations
 * require no CUDA SDK; native addresses and driver handles stay in providers.
 */
#pragma once

#include "ArdaRHIResources.h"
#include <type_traits>

namespace arda
{
    /** Selects CUDA scheduling once, before resources and command lists are created. */
    enum class EArdaCudaExecutionMode : uint8_t
    {
        /** Prefer graphics-queue execution; fall back when context or surface qualification fails. */
        Automatic,
        /** Require the provider's native graphics-queue CUDA path. */
        GraphicsQueue,
        /** Use an ordinary CUDA context and serialize graphics/CUDA submission segments. */
        ContextSwitch
    };

    /** The qualified native execution path selected for this device. */
    enum class EArdaCudaLaunchMode : uint8_t
    {
        /** CUDA is disabled, unavailable, or rejected by device admission. */
        None,
        /** CUDA in Graphics capture on the D3D12 graphics queue. */
        D3D12CiG,
        /** VK_NV_cuda_kernel_launch on a Vulkan graphics or compute queue. */
        VulkanKernel,
        /** An ordinary CUDA context; graphics and CUDA segments execute separately. */
        ContextSwitch
    };

    /** Qualified launch mode and limits for this device, independent of graphics capabilities. */
    struct FArdaCudaCapabilities
    {
        /** None disables CUDA selection without disabling graphics compute. */
        EArdaCudaLaunchMode mLaunchMode = EArdaCudaLaunchMode::None;
        /** Explains why automatic selection used a context-switching fallback. */
        eastl::string mFallbackReason;
        /** CUDA architecture encoded as major * 10 + minor; SM 12.0 is 120. */
        uint32_t mComputeCapability = 0;
        /** Maximum product of the three block dimensions. */
        uint32_t mMaxThreadsPerBlock = 0;
        /** Inclusive per-axis limits for block dimensions, in threads. */
        uint32_t mMaxBlockSize[3] = {};
        /** Inclusive per-axis limits for grid dimensions, in blocks. */
        uint32_t mMaxGridSize[3] = {};
        /** Maximum explicitly requested dynamic shared memory per block, in bytes. */
        uint32_t mMaxSharedMemoryBytes = 0;
        /** True only when native surface mapping/handles have been qualified. */
        bool mbSurfaceAccess = false;
        /** True when layered CUDA surfaces are qualified in addition to ordinary surfaces. */
        bool mbLayeredSurfaceAccess = false;
        /** Diagnostic explaining surface exclusion; buffer launches may still work. */
        eastl::string mSurfaceUnavailableReason = "CUDA surfaces were not enabled by this provider.";
        /** Diagnostic explaining why no CUDA launch mode is available. */
        eastl::string mUnavailableReason = "CUDA launch support was not enabled by this provider.";
        /** True when this device admits a CUDA launch mode; does not imply surface support. */
        explicit operator bool() const noexcept { return mLaunchMode != EArdaCudaLaunchMode::None; }
    };

    /** Declared resource access used to order a CUDA dispatch with graphics work. */
    enum class EArdaComputeAccess : uint8_t
    {
        /** The kernel only loads from the binding. */
        Read,
        /** The kernel stores to the binding without consuming its previous contents. */
        Write,
        /** The kernel may both load and store, including in-place updates. */
        ReadWrite
    };
    /** Shader-independent kind of a CUDA resource binding. */
    enum class EArdaComputeBindingType : uint8_t
    {
        /** A byte range of an IArdaRHIBuffer. */
        Buffer,
        /** One texture mip exposed as a CUDA surface, including its layers/depth. */
        Surface
    };
    /** Raw channel interpretation; normalization and color conversion are not implicit. */
    enum class EArdaCudaScalarType : uint8_t
    {
        /** Unsigned integer channel. */
        UInt,
        /** Signed integer channel. */
        SInt,
        /** IEEE floating-point channel. */
        Float
    };
    /** Native channel layout for a CUDA-compatible storage format. */
    struct FArdaCudaFormatInfo
    {
        /** Arithmetic interpretation of each stored channel. */
        EArdaCudaScalarType mScalarType = EArdaCudaScalarType::UInt;
        /** Bits per channel, not bits per pixel. */
        uint32_t mBits = 0;
        /** Channel count; zero means the RHI format cannot be used as a CUDA surface. */
        uint32_t mChannels = 0;
    };
    /** Returns the shared format admission table entry; unsupported formats have zero channels. */
    [[nodiscard]] FArdaCudaFormatInfo GetArdaCudaFormatInfo(EArdaRHIFormat Format) noexcept;

    /** A retained resource view; CUDA addresses remain private to the native provider. */
    struct FArdaCudaBinding
    {
        /** Retains the source buffer or texture; it must belong to the command-list device. */
        FArdaRHIResourceRef mResource;
        /** Access declaration supplied by the dispatch implementation, matching its parameter contract. */
        EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
        /** Buffer-only byte range; the byte offset is incorporated into the kernel address. */
        FArdaRHIBufferRange mBufferRange;
        /** Texture-only mip index. All layers or depth slices of that mip remain addressable. */
        uint32_t mMipLevel = 0;
    };

    /** Either a typed resource binding or owned scalar/POD argument bytes. */
    struct FArdaCudaArgument
    {
        /** Index into FArdaCudaDispatch::mBindings, or UINT32_MAX for owned value bytes. */
        uint32_t mBindingIndex = UINT32_MAX;
        /** Exact PTX parameter bytes; empty for a resource-binding argument. */
        eastl::vector<uint8_t> mValue;
        /** Selects a retained binding; the provider supplies its 64-bit pointer/surface value. */
        static FArdaCudaArgument Binding(uint32_t Index) { return {Index, {}}; }
        /** Copies a trivially copyable value. Its byte size/layout must match the PTX parameter ABI. */
        template<class T> static FArdaCudaArgument Value(const T& Value)
        {
            static_assert(std::is_trivially_copyable_v<T>, "Kernel values must be trivially copyable.");
            const auto* Bytes = reinterpret_cast<const uint8_t*>(&Value);
            FArdaCudaArgument Result;
            Result.mValue.assign(Bytes, Bytes + sizeof(T));
            return Result;
        }
    };

    /** One PTX entry-point launch; owns code and arguments through command recording. */
    struct FArdaCudaKernel
    {
        /** PTX source bytes; embedded NULs are rejected. No CUDA-C compiler is invoked. */
        eastl::string mPtx;
        /** Exact exported PTX entry-point name. */
        eastl::string mEntryPoint;
        /** Number of blocks in each axis; each dimension must be nonzero. */
        uint32_t mGridSize[3] = {1, 1, 1};
        /** Threads per block in each axis; per-axis and product limits both apply. */
        uint32_t mBlockSize[3] = {1, 1, 1};
        /** Dynamic shared-memory request for each block, in bytes. */
        uint32_t mSharedMemoryBytes = 0;
        /** Ordered arguments matching the PTX entry point, including unused parameters. */
        eastl::vector<FArdaCudaArgument> mArguments;
    };

    /** Kernels execute in order; write/read dependencies between them are synchronized. */
    struct FArdaCudaDispatch
    {
        /** Resource views shared by all kernels in this dispatch. */
        eastl::vector<FArdaCudaBinding> mBindings;
        /** Nonempty launch sequence recorded in this order. */
        eastl::vector<FArdaCudaKernel> mKernels;
    };

    /** Rejects unsupported texture layouts/formats before native allocation; does not test device support. */
    [[nodiscard]] FArdaRHIStatus ValidateArdaCudaTexture(const FArdaRHITextureDesc& Desc);
    /** Admits dedicated device-local buffers; rejects sparse, placed, versioned, CPU-visible and AS storage. */
    [[nodiscard]] FArdaRHIStatus ValidateArdaCudaBuffer(const FArdaRHIBufferDesc& Desc);
    /** Checks code, dimensions, shared memory and argument references; does not prove kernel memory safety. */
    [[nodiscard]] FArdaRHIStatus ValidateArdaCudaKernels(
        const eastl::vector<FArdaCudaKernel>& Kernels, size_t BindingCount,
        const FArdaCudaCapabilities& Capabilities);
}
