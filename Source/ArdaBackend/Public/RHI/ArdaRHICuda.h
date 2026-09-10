/** @file ArdaRHICuda.h
 * Describes CUDA launches recorded into an RHI command list. Public declarations
 * require no CUDA SDK; native addresses and driver handles stay in providers.
 */
#pragma once

#include "ArdaRHIResources.h"
#include <type_traits>
#include <typeinfo>

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
        /** CUDA stream joined to a Vulkan external compute queue. */
        VulkanCiG,
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

    /** Native binary target emitted by the build; accelerated targets require an exact match. */
    struct FArdaCudaArchitecture
    {
        /** Major times ten plus minor. */
        uint32_t mComputeCapability = 0;
        /** Architecture/family-specific code is admitted only on its exact build target. */
        bool mbExact = false;
        /** Tests binary compatibility conservatively, without requesting driver compilation. */
        bool Supports(uint32_t DeviceCapability) const noexcept;
    };
    /** Build-generated identity and native-code coverage for a compilation profile. */
    struct FArdaCudaBuildInfo
    {
        /** Unique profile name; also isolates symbols compiled with different flags. */
        eastl::string mName;
        /** Compiler/options/source-build fingerprint for diagnostics and caches. */
        eastl::string mIdentity;
        /** Native targets actually requested by the build. */
        eastl::vector<FArdaCudaArchitecture> mArchitectures;
        /** True when the build opts into approximate floating-point operations. */
        bool mbFastMath = false;
    };
    /** Standard launch geometry. Streams, contexts and submission remain framework-owned. */
    struct FArdaCudaLaunchConfig
    {
        /** Number of blocks in each axis; each dimension must be nonzero. */
        uint32_t mGridSize[3] = {1, 1, 1};
        /** Threads per block in each axis; per-axis and product limits both apply. */
        uint32_t mBlockSize[3] = {1, 1, 1};
        /** Dynamic shared-memory request for each block, in bytes. */
        uint32_t mSharedMemoryBytes = 0;
    };
    /** Runtime signature of the one by-value parameter accepted by a compiled entry. */
    struct FArdaCudaKernelSignature
    {
        /** C++ type identity, compared at registration; null denotes an unsupported signature. */
        const std::type_info* mType = nullptr;
        /** Size of the CUDA argument object. */
        size_t mSize = 0;
        /** Alignment of the CUDA argument object. */
        size_t mAlignment = 1;
        /** Runtime eligibility; no facade static assertion is required. */
        bool mbSupported = false;
    };
    /** Kernel-specific native limits queried under the execution context. */
    struct FArdaCudaKernelLimits
    {
        /** Maximum threads for this compiled function. */
        uint32_t mMaxThreadsPerBlock = 0;
        /** Static shared memory used by the function. */
        uint32_t mStaticSharedMemoryBytes = 0;
        /** Maximum dynamic shared memory accepted by the function. */
        uint32_t mMaxDynamicSharedMemoryBytes = 0;
    };
    /** Provider-facing compiled launch adapter. Implemented by the nvcc registration helper.
     * The current CUDA context is supplied by the provider; the adapter never selects a device.
     * Retain this object and its owning code module through GPU completion.
     */
    class IArdaCudaKernelEntry
    {
    public:
        virtual ~IArdaCudaKernelEntry() = default;
        /** Reports the registered signature without initializing CUDA. */
        virtual FArdaCudaKernelSignature GetSignature() const noexcept = 0;
        /** Returns the immutable build manifest. */
        virtual const FArdaCudaBuildInfo& GetBuildInfo() const noexcept = 0;
        /** Queries limits in the provider's current context; no kernel is launched. */
        virtual TArdaRHIResult<FArdaCudaKernelLimits> GetLimits() const = 0;
        /** Enqueues exactly one compiled kernel on the borrowed opaque CUDA stream. */
        virtual FArdaRHIStatus Launch(void* Stream, const FArdaCudaLaunchConfig& Config,
            const void* Parameters, size_t ParameterSize) const = 0;
    };
    /** Patches one resource address/surface into the owned CUDA parameter object. */
    struct FArdaCudaParameterPatch
    {
        /** Retained resource index in the dispatch. */
        uint32_t mBindingIndex = 0;
        /** Byte offset of the 64-bit CUDA resource representation. */
        size_t mOffset = 0;
        /** Required native buffer address alignment. Surfaces use one. */
        size_t mAlignment = 1;
    };
    /** One precompiled kernel and an owned, unresolved by-value CUDA parameter object. */
    struct FArdaCudaKernel : FArdaCudaLaunchConfig
    {
        /** Retained compiled entry; source compilation and string entry lookup are unavailable. */
        eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry;
        /** Frozen values; resource representations are patched by the provider. */
        eastl::vector<uint8_t> mParameters;
        /** Native resource locations inside the parameter object. */
        eastl::vector<FArdaCudaParameterPatch> mPatches;
    };
    /** A single kernel dispatch; multiple operations must be recorded separately. */
    struct FArdaCudaDispatch
    {
        /** Resource views retained by the kernel in this dispatch. */
        eastl::vector<FArdaCudaBinding> mBindings;
        /** Exactly one launch. The vector preserves the provider ABI's batch representation. */
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
