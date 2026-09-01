/** @file ArdaRHITypes.h
 * Declares backend-neutral RHI enums, value types, descriptors, hashing, and validation helpers.
 */

#pragma once

#include <EASTL/string.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace arda::rhi
{
    /** Forward declaration of texture desc. */
    struct FArdaRHITextureDesc;
    /** Forward declaration of buffer desc. */
    struct FArdaRHIBufferDesc;

    /**
     * Performs the max operation.
     * @return The requested numeric value.
     */
    inline constexpr uint32_t ArdaRHIAllSubresources = std::numeric_limits<uint32_t>::max();
    /**
     * Performs the max operation.
     * @return The requested numeric value.
     */
    inline constexpr uint64_t ArdaRHIWholeBuffer = std::numeric_limits<uint64_t>::max();
    /** Maximum number of simultaneous render targets. */
    inline constexpr uint32_t ArdaRHIMaxRenderTargets = 8;

    /** Enumerates result values. */
    enum class EArdaRHIResult : uint8_t
    {
        Success,
        InvalidArgument,
        Unsupported,
        BackendFailure,
        InvalidState,
        WrongDevice
    };

    /** Describes status. */
    struct FArdaRHIStatus
    {
        /** Stores the code. */
        EArdaRHIResult mCode = EArdaRHIResult::Success;
        /** Stores the message. */
        eastl::string mMessage;

        /**
         * Tests whether the status represents success.
         * @return True when the condition is satisfied; otherwise false.
         */
        [[nodiscard]] bool IsSuccess() const noexcept { return mCode == EArdaRHIResult::Success; }
        /**
         * Converts the status to a success flag.
         * @return True when the reference or result is valid; otherwise false.
         */
        [[nodiscard]] explicit operator bool() const noexcept { return IsSuccess(); }
        /**
         * Performs the success operation.
         * @return A status describing whether the operation succeeded.
         */
        static FArdaRHIStatus Success() { return {}; }
        /**
         * Performs the error operation.
         * @param Code The code.
         * @param Message The message.
         * @return A status describing whether the operation succeeded.
         */
        static FArdaRHIStatus Error(EArdaRHIResult Code, const char* Message)
        {
            return { Code, Message ? Message : "" };
        }
    };

    /** Describes result. */
    template <typename T>
    struct TArdaRHIResult
    {
        /** Stores the value. */
        T mValue{};
        /** Stores the status. */
        FArdaRHIStatus mStatus;
        /**
         * Converts the result to a success flag.
         * @return True when the reference or result is valid; otherwise false.
         */
        [[nodiscard]] explicit operator bool() const noexcept { return mStatus.IsSuccess(); }
    };

    /** Enumerates format values. */
    enum class EArdaRHIFormat : uint8_t
    {
        Unknown,
        R8UInt, R8SInt, R8UNorm, R8SNorm,
        RG8UInt, RG8SInt, RG8UNorm, RG8SNorm,
        R16UInt, R16SInt, R16UNorm, R16SNorm, R16Float,
        RGBA8UInt, RGBA8SInt, RGBA8UNorm, RGBA8SNorm,
        BGRA8UNorm, SRGBA8UNorm, SBGRA8UNorm,
        R10G10B10A2UNorm, R11G11B10Float,
        RG16UInt, RG16SInt, RG16UNorm, RG16SNorm, RG16Float,
        R32UInt, R32SInt, R32Float,
        RGBA16UInt, RGBA16SInt, RGBA16Float, RGBA16UNorm, RGBA16SNorm,
        RG32UInt, RG32SInt, RG32Float,
        RGB32UInt, RGB32SInt, RGB32Float,
        RGBA32UInt, RGBA32SInt, RGBA32Float,
        D16, D24S8, D32, D32S8,
        BC1UNorm, BC1UNormSRGB, BC2UNorm, BC2UNormSRGB,
        BC3UNorm, BC3UNormSRGB, BC4UNorm, BC4SNorm,
        BC5UNorm, BC5SNorm, BC6HUFloat, BC6HSFloat, BC7UNorm, BC7UNormSRGB,
        /** Number of format values, including Unknown. */
        Count
    };
    /** @return True for a usable format value rather than a sentinel. */
    [[nodiscard]] inline constexpr bool IsArdaRHIFormatKnown(
        EArdaRHIFormat Format) noexcept
    {
        return Format > EArdaRHIFormat::Unknown &&
            Format < EArdaRHIFormat::Count;
    }

    /** Enumerates texture dimension values. */
    enum class EArdaRHITextureDimension : uint8_t
    {
        Unknown, Texture1D, Texture1DArray, Texture2D, Texture2DArray,
        TextureCube, TextureCubeArray, Texture2DMS, Texture2DMSArray, Texture3D
    };

    /** Enumerates CPU access values. */
    enum class EArdaRHICpuAccess : uint8_t { None, Read, Write };
    /** Enumerates queue type values. */
    enum class EArdaRHIQueueType : uint8_t
    {
        Graphics = 0,
        Compute = 1,
        Copy = 2,
        Count
    };
    /** Number of queue types represented by EArdaRHIQueueType. */
    inline constexpr size_t ArdaRHIQueueTypeCount =
        static_cast<size_t>(EArdaRHIQueueType::Count);
    /** @return The canonical array index for a queue type. */
    [[nodiscard]] inline constexpr size_t GetArdaRHIQueueIndex(
        EArdaRHIQueueType Queue) noexcept
    {
        return static_cast<size_t>(Queue);
    }
    /** Pipeline domains participating in a resource transition. */
    enum class EArdaRHIPipeline : uint8_t
    {
        None = 0,
        Graphics = 1u << 0,
        AsyncCompute = 1u << 1,
        Copy = 1u << 2,
        All = 0x07
    };
    /** Optional transition scheduling and lifetime semantics. */
    enum class EArdaRHITransitionFlags : uint8_t
    {
        None = 0,
        BeginOnly = 1u << 0,
        EndOnly = 1u << 1,
        Discard = 1u << 2
    };
    /** Enumerates heap type values. */
    enum class EArdaRHIHeapType : uint8_t { DeviceLocal, Upload, Readback };
    /** Enumerates bindless layout type values. */
    enum class EArdaRHIBindlessLayoutType : uint8_t
    {
        Immutable, MutableSrvUavCbv, MutableCounters, MutableSampler
    };
    /** Identifies the backend representation of an imported native resource. */
    enum class EArdaRHINativeResourceType : uint8_t
    {
        /** Native object and payload interpreted by the selected backend module. */
        BackendDefined,
        /** Standard Direct3D 12 resource object. */
        D3D12Resource,
        VulkanImage,
        VulkanBuffer,
        D3D12AccelerationStructure,
        VulkanAccelerationStructure,
        VulkanOpacityMicromap
    };
    /** Controls whether an imported native resource remains caller-owned. */
    enum class EArdaRHINativeOwnership : uint8_t
    {
        Borrowed,
        Transferred
    };
    /** Enumerates sampler feedback format values. */
    enum class EArdaRHISamplerFeedbackFormat : uint8_t
    {
        MinMipOpaque, MipRegionUsedOpaque
    };
    /** Enumerates ray tracing geometry type values. */
    enum class EArdaRHIRayTracingGeometryType : uint8_t { Triangles, AABBs };
    /** Enumerates opacity micromap format values. */
    enum class EArdaRHIOpacityMicromapFormat : uint8_t { TwoState = 1, FourState = 2 };
    /** Enumerates shader stage values. */
    enum class EArdaRHIShaderStage : uint16_t
    {
        None = 0, Vertex = 1u << 0, Hull = 1u << 1, Domain = 1u << 2,
        Geometry = 1u << 3, Pixel = 1u << 4, Compute = 1u << 5,
        Amplification = 1u << 6, Mesh = 1u << 7,
        RayGeneration = 1u << 8, AnyHit = 1u << 9,
        ClosestHit = 1u << 10, Miss = 1u << 11,
        Intersection = 1u << 12, Callable = 1u << 13,
        WorkGraph = 1u << 14,
        AllGraphics = 0x00df, AllRayTracing = 0x3f00, All = 0x7fff
    };
    /** @return True when Stage names exactly one ray-tracing shader stage. */
    [[nodiscard]] inline constexpr bool IsArdaRHIRayTracingShaderStage(
        EArdaRHIShaderStage Stage) noexcept
    {
        const uint16_t Value = static_cast<uint16_t>(Stage);
        const uint16_t RayStages =
            static_cast<uint16_t>(EArdaRHIShaderStage::AllRayTracing);
        return Value != 0 && (Value & (Value - 1)) == 0 &&
            (Value & RayStages) == Value;
    }

    /** Enumerates resource state values. */
    enum class EArdaRHIResourceState : uint32_t
    {
        Unknown = 0, Common = 1u << 0, ConstantBuffer = 1u << 1,
        VertexBuffer = 1u << 2, IndexBuffer = 1u << 3,
        IndirectArgument = 1u << 4, PixelShaderResource = 1u << 5,
        NonPixelShaderResource = 1u << 6,
        ShaderResource = (1u << 5) | (1u << 6),
        UnorderedAccess = 1u << 7, RenderTarget = 1u << 8,
        DepthWrite = 1u << 9, DepthRead = 1u << 10,
        CopyDest = 1u << 11, CopySource = 1u << 12,
        ResolveDest = 1u << 13, ResolveSource = 1u << 14, Present = 1u << 15,
        AccelStructRead = 1u << 16, AccelStructWrite = 1u << 17,
        AccelStructBuildInput = 1u << 18, AccelStructBuildBlas = 1u << 19,
        CpuRead = 1u << 20,
        OpacityMicromapWrite = 1u << 21, OpacityMicromapBuildInput = 1u << 22,
        Discard = 1u << 23, ShadingRateSource = 1u << 24
    };

    /** Describes the state independently tracked by a native backend. */
    struct FArdaRHINativeResourceState
    {
        /** Abstract Arda state represented by the backend tracker. */
        EArdaRHIResourceState mState = EArdaRHIResourceState::Unknown;
        /** Native resource representation that owns the encoded state. */
        EArdaRHINativeResourceType mNativeType =
            EArdaRHINativeResourceType::BackendDefined;
        /** D3D12 state bits or Vulkan image layout, depending on native type. */
        uint64_t mPrimaryState = 0;
        /** Native synchronization pipeline-stage mask, when applicable. */
        uint64_t mPipelineStageMask = 0;
        /** Native synchronization access mask, when applicable. */
        uint64_t mAccessMask = 0;
        /** Owning Vulkan queue family, or 0xffffffff for APIs without families. */
        uint32_t mQueueFamily = 0xffffffffu;
        /** Whether the backend has an authoritative state for the range. */
        bool mbKnown = false;
        /** Whether the encoded native values are valid for mState. */
        bool mbNativeCompatible = false;
    };

    /** Describes independently observed facade and native resource state. */
    struct FArdaRHIResourceStateSnapshot
    {
        /** State maintained by the common ArdaRHI facade tracker. */
        EArdaRHIResourceState mFacadeState = EArdaRHIResourceState::Unknown;
        /** Queue whose command list produced this observation. */
        EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
        /** Queue that owns the resource after the recorded operation. */
        EArdaRHIQueueType mFacadeQueueOwner = EArdaRHIQueueType::Graphics;
        /** Whether facade queue ownership is authoritative. */
        bool mbFacadeQueueOwnerKnown = false;
        /** Native backend tracker and exact barrier encoding. */
        FArdaRHINativeResourceState mNative;
        /** Whether the facade has an authoritative state for the range. */
        bool mbFacadeKnown = false;

        /**
         * Tests whether every independently tracked layer agrees.
         * @return True when facade, backend, and native encoding are consistent.
         */
        [[nodiscard]] bool IsConsistent() const noexcept
        {
            return mbFacadeKnown && mNative.mbKnown &&
                mNative.mbNativeCompatible &&
                mFacadeState == mNative.mState;
        }
    };

    /** Enumerates texture usage values. */
    enum class EArdaRHITextureUsage : uint16_t
    {
        None = 0, ShaderResource = 1u << 0, UnorderedAccess = 1u << 1,
        RenderTarget = 1u << 2, DepthStencil = 1u << 3,
        Typeless = 1u << 4
    };

    /** Enumerates buffer usage values. */
    enum class EArdaRHIBufferUsage : uint16_t
    {
        None = 0, ShaderResource = 1u << 0, UnorderedAccess = 1u << 1,
        Vertex = 1u << 2, Index = 1u << 3, Constant = 1u << 4,
        Indirect = 1u << 5, Raw = 1u << 6, Structured = 1u << 7,
        Volatile = 1u << 8, AccelStructBuildInput = 1u << 9,
        AccelStructStorage = 1u << 10, ShaderBindingTable = 1u << 11,
        OpacityMicromapBuildInput = 1u << 12
    };

    /** Enumerates ray tracing geometry flags values. */
    enum class EArdaRHIRayTracingGeometryFlags : uint8_t
    {
        None = 0, Opaque = 1u << 0, NoDuplicateAnyHitInvocation = 1u << 1
    };
    /** Enumerates accel struct build flags values. */
    enum class EArdaRHIAccelStructBuildFlags : uint8_t
    {
        None = 0, AllowUpdate = 1u << 0, AllowCompaction = 1u << 1,
        PreferFastTrace = 1u << 2, PreferFastBuild = 1u << 3,
        MinimizeMemory = 1u << 4, PerformUpdate = 1u << 5,
        AllowEmptyInstances = 1u << 7
    };
    /** Enumerates opacity micromap build flags values. */
    enum class EArdaRHIOpacityMicromapBuildFlags : uint8_t
    {
        None = 0, FastTrace = 1u << 0, FastBuild = 1u << 1,
        AllowCompaction = 1u << 2
    };

/** Generates bitwise operators and flag testing for a scoped RHI enum. */
#define ARDA_RHI_FLAG_OPERATORS(Type) \
    /** Combines two flag sets. @param A Left flag set. @param B Right flag set. @return The combined flags. */ \
    constexpr Type operator|(Type A, Type B) noexcept { return static_cast<Type>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B)); } \
    /** Intersects two flag sets. @param A Left flag set. @param B Right flag set. @return The common flags. */ \
    constexpr Type operator&(Type A, Type B) noexcept { return static_cast<Type>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B)); } \
    /** Adds flags to a flag set. @param A Flag set to update. @param B Flags to add. @return The updated flag set. */ \
    constexpr Type& operator|=(Type& A, Type B) noexcept { A = A | B; return A; } \
    /** Tests whether any requested flag is set. @param Value Flag set to inspect. @param Flags Flags to test. @return True when any requested flag is set. */ \
    constexpr bool HasAnyFlags(Type Value, Type Flags) noexcept { return static_cast<uint32_t>(Value & Flags) != 0; }

    ARDA_RHI_FLAG_OPERATORS(EArdaRHIShaderStage)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIResourceState)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIPipeline)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHITransitionFlags)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHITextureUsage)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIBufferUsage)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIRayTracingGeometryFlags)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIAccelStructBuildFlags)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIOpacityMicromapBuildFlags)
#undef ARDA_RHI_FLAG_OPERATORS

    /** Enumerates sampler address mode values. */
    enum class EArdaRHISamplerAddressMode : uint8_t { Clamp, Wrap, Border, Mirror, MirrorOnce };
    /** Enumerates sampler reduction values. */
    enum class EArdaRHISamplerReduction : uint8_t { Standard, Comparison, Minimum, Maximum };
    /** Enumerates primitive topology values. */
    enum class EArdaRHIPrimitiveTopology : uint8_t { PointList, LineList, LineStrip, TriangleList, TriangleStrip, PatchList };
    /** Enumerates fill mode values. */
    enum class EArdaRHIFillMode : uint8_t { Solid, Wireframe };
    /** Enumerates cull mode values. */
    enum class EArdaRHICullMode : uint8_t { Back, Front, None };
    /** Enumerates comparison func values. */
    enum class EArdaRHIComparisonFunc : uint8_t { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };
    /** Enumerates blend factor values. */
    enum class EArdaRHIBlendFactor : uint8_t
    {
        Zero, One, SourceColor, InverseSourceColor, SourceAlpha,
        InverseSourceAlpha, DestinationAlpha, InverseDestinationAlpha,
        DestinationColor, InverseDestinationColor
    };
    /** Enumerates binding type values. */
    enum class EArdaRHIBindingType : uint8_t
    {
        TextureSRV, TextureUAV, TypedBufferSRV, TypedBufferUAV,
        StructuredBufferSRV, StructuredBufferUAV, RawBufferSRV, RawBufferUAV,
        ConstantBuffer, VolatileConstantBuffer, Sampler, PushConstants,
        RayTracingAccelStruct, SamplerFeedbackTextureUAV
    };

    /** Describes color. */
    struct FArdaRHIColor
    {
        /** Red component. */
        float mR = 0.f;
        /** Green component. */
        float mG = 0.f;
        /** Blue component. */
        float mB = 0.f;
        /** Alpha component. */
        float mA = 0.f;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIColor& O) const noexcept { return mR == O.mR && mG == O.mG && mB == O.mB && mA == O.mA; }
    };

    /** Describes texture subresource range. */
    struct FArdaRHITextureSubresourceRange
    {
        /** Stores the base mip level. */
        uint32_t mBaseMipLevel = 0;
        /** Stores the mip level count. */
        uint32_t mMipLevelCount = ArdaRHIAllSubresources;
        /** Stores the base array slice. */
        uint32_t mBaseArraySlice = 0;
        /** Stores the array slice count. */
        uint32_t mArraySliceCount = ArdaRHIAllSubresources;
        /** First format plane (depth is zero and stencil is one). */
        uint32_t mBasePlane = 0;
        /** Number of format planes. */
        uint32_t mPlaneCount = ArdaRHIAllSubresources;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHITextureSubresourceRange& O) const noexcept
        {
            return mBaseMipLevel == O.mBaseMipLevel && mMipLevelCount == O.mMipLevelCount &&
                mBaseArraySlice == O.mBaseArraySlice && mArraySliceCount == O.mArraySliceCount &&
                mBasePlane == O.mBasePlane && mPlaneCount == O.mPlaneCount;
        }
        /**
         * Performs the resolve operation.
         * @param Desc The desc.
         * @return The requested value.
         */
        [[nodiscard]] FArdaRHITextureSubresourceRange Resolve(
            const FArdaRHITextureDesc& Desc) const noexcept;
    };

    /** Describes buffer range. */
    struct FArdaRHIBufferRange
    {
        /** Stores the byte offset. */
        uint64_t mByteOffset = 0;
        /** Stores the byte size. */
        uint64_t mByteSize = ArdaRHIWholeBuffer;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIBufferRange& O) const noexcept { return mByteOffset == O.mByteOffset && mByteSize == O.mByteSize; }
        /**
         * Performs the resolve operation.
         * @param Desc The desc.
         * @return The requested value.
         */
        [[nodiscard]] FArdaRHIBufferRange Resolve(const FArdaRHIBufferDesc& Desc) const noexcept;
        /**
         * Tests whether the whole buffer.
         * @param Desc The desc.
         * @return True when the condition is satisfied; otherwise false.
         */
        [[nodiscard]] bool IsWholeBuffer(const FArdaRHIBufferDesc& Desc) const noexcept;
    };

    /** Describes texture desc. */
    struct FArdaRHITextureDesc
    {
        /** Texture width in texels. */
        uint32_t mWidth = 1;
        /** Texture height in texels. */
        uint32_t mHeight = 1;
        /** Texture depth in texels. */
        uint32_t mDepth = 1;
        /** Number of array slices. */
        uint32_t mArraySize = 1;
        /** Number of mip levels. */
        uint32_t mMipLevels = 1;
        /** Multisample count. */
        uint32_t mSampleCount = 1;
        /** Stores the format. */
        EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
        /** Stores the dimension. */
        EArdaRHITextureDimension mDimension = EArdaRHITextureDimension::Texture2D;
        /** Stores the usage. */
        EArdaRHITextureUsage mUsage = EArdaRHITextureUsage::ShaderResource;
        /** Stores the initial state. */
        EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
        /** Stores the keep initial state. */
        bool mbKeepInitialState = false;
        /** Stores the virtual. */
        bool mbVirtual = false;
        /** Stores the tiled. */
        bool mbTiled = false;
        /** Stores the clear value. */
        FArdaRHIColor mClearValue;
        /** Stores the use clear value. */
        bool mbUseClearValue = false;
        /** Stores the debug name. */
        eastl::string mDebugName;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHITextureDesc& O) const noexcept;
    };

    /** Describes buffer desc. */
    struct FArdaRHIBufferDesc
    {
        /** Stores the byte size. */
        uint64_t mByteSize = 0;
        /** Structured-buffer element stride in bytes. */
        uint32_t mStructureStride = 0;
        /** Maximum number of backing-buffer versions. */
        uint32_t mMaxVersions = 0;
        /** Stores the format. */
        EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
        /** Stores the usage. */
        EArdaRHIBufferUsage mUsage = EArdaRHIBufferUsage::None;
        /** Stores the CPU access. */
        EArdaRHICpuAccess mCpuAccess = EArdaRHICpuAccess::None;
        /** Stores the initial state. */
        EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Common;
        /** Stores the keep initial state. */
        bool mbKeepInitialState = false;
        /** Stores the virtual. */
        bool mbVirtual = false;
        /** Creates a sparse/reserved buffer committed in physical tiles. */
        bool mbTiled = false;
        /** Stores the debug name. */
        eastl::string mDebugName;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIBufferDesc& O) const noexcept;
    };

    /** Describes view desc. */
    struct FArdaRHIViewDesc
    {
        /** Stores the format. */
        EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
        /** Stores the dimension. */
        EArdaRHITextureDimension mDimension = EArdaRHITextureDimension::Unknown;
        /** Stores the texture range. */
        FArdaRHITextureSubresourceRange mTextureRange;
        /** Stores the buffer range. */
        FArdaRHIBufferRange mBufferRange;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIViewDesc& O) const noexcept
        {
            return mFormat == O.mFormat && mDimension == O.mDimension &&
                mTextureRange == O.mTextureRange && mBufferRange == O.mBufferRange;
        }
    };

    /** Backend-neutral description of a native texture import. */
    struct FArdaRHINativeTextureImportDesc
    {
        /** Stores the native object. */
        uintptr_t mNativeObject = 0;
        /** Stores the native type. */
        EArdaRHINativeResourceType mNativeType = EArdaRHINativeResourceType::BackendDefined;
        /** Stable native type name required when mNativeType is BackendDefined. */
        eastl::string mNativeTypeName;
        /** Immutable backend-specific metadata copied into the import request. */
        eastl::vector<uint8_t> mBackendData;
        /** Stores the ownership. */
        EArdaRHINativeOwnership mOwnership = EArdaRHINativeOwnership::Borrowed;
        /** Stores the texture. */
        FArdaRHITextureDesc mTexture;
        /** Stores the initial state. */
        EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
        /**
         * Optional shared token retained by the imported wrapper.
         * The token, rather than Arda, owns any native lifetime it represents.
         */
        eastl::shared_ptr<void> mLifetimeToken;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHINativeTextureImportDesc& O) const noexcept
        {
            return mNativeObject == O.mNativeObject && mNativeType == O.mNativeType &&
                mNativeTypeName == O.mNativeTypeName && mBackendData == O.mBackendData &&
                mOwnership == O.mOwnership && mTexture == O.mTexture &&
                mInitialState == O.mInitialState &&
                !mLifetimeToken.owner_before(O.mLifetimeToken) &&
                !O.mLifetimeToken.owner_before(mLifetimeToken);
        }
    };

    /** Backend-neutral description of a native buffer import. */
    struct FArdaRHINativeBufferImportDesc
    {
        /** Stores the native object. */
        uintptr_t mNativeObject = 0;
        /** Stores the native type. */
        EArdaRHINativeResourceType mNativeType = EArdaRHINativeResourceType::BackendDefined;
        /** Stable native type name required when mNativeType is BackendDefined. */
        eastl::string mNativeTypeName;
        /** Immutable backend-specific metadata copied into the import request. */
        eastl::vector<uint8_t> mBackendData;
        /** Stores the ownership. */
        EArdaRHINativeOwnership mOwnership = EArdaRHINativeOwnership::Borrowed;
        /** Stores the buffer. */
        FArdaRHIBufferDesc mBuffer;
        /** Stores the initial state. */
        EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
        /**
         * Optional shared token retained by the imported wrapper.
         * The token, rather than Arda, owns any native lifetime it represents.
         */
        eastl::shared_ptr<void> mLifetimeToken;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHINativeBufferImportDesc& O) const noexcept
        {
            return mNativeObject == O.mNativeObject && mNativeType == O.mNativeType &&
                mNativeTypeName == O.mNativeTypeName && mBackendData == O.mBackendData &&
                mOwnership == O.mOwnership && mBuffer == O.mBuffer &&
                mInitialState == O.mInitialState &&
                !mLifetimeToken.owner_before(O.mLifetimeToken) &&
                !O.mLifetimeToken.owner_before(mLifetimeToken);
        }
    };

    /** Describes texture slice. */
    struct FArdaRHITextureSlice
    {
        /** X origin in texels. */
        uint32_t mX = 0;
        /** Y origin in texels. */
        uint32_t mY = 0;
        /** Z origin in texels. */
        uint32_t mZ = 0;
        /** Stores the width. */
        uint32_t mWidth = ArdaRHIAllSubresources;
        /** Stores the height. */
        uint32_t mHeight = ArdaRHIAllSubresources;
        /** Stores the depth. */
        uint32_t mDepth = ArdaRHIAllSubresources;
        /** Mip level containing the slice. */
        uint32_t mMipLevel = 0;
        /** Array slice containing the region. */
        uint32_t mArraySlice = 0;
        /** Format plane containing the region. */
        uint32_t mPlane = 0;
    };

    /** Concrete extent resolved for a texture-region copy. */
    struct FArdaRHITextureCopyExtent
    {
        uint32_t mWidth = 0;
        uint32_t mHeight = 0;
        uint32_t mDepth = 0;
    };

    /** Explicit texture transition including expected state and pipeline domains. */
    struct FArdaRHITextureTransitionDesc
    {
        FArdaRHITextureSubresourceRange mSubresources;
        EArdaRHIResourceState mStateBefore = EArdaRHIResourceState::Unknown;
        EArdaRHIResourceState mStateAfter = EArdaRHIResourceState::Unknown;
        EArdaRHIPipeline mSourcePipelines = EArdaRHIPipeline::Graphics;
        EArdaRHIPipeline mDestinationPipelines = EArdaRHIPipeline::Graphics;
        EArdaRHITransitionFlags mFlags = EArdaRHITransitionFlags::None;
        /** Source queue for a paired queue-family release/acquire transfer. */
        EArdaRHIQueueType mSourceQueue = EArdaRHIQueueType::Graphics;
        /** Destination queue for a paired queue-family release/acquire transfer. */
        EArdaRHIQueueType mDestinationQueue = EArdaRHIQueueType::Graphics;
        /** True when this transition transfers native queue-family ownership. */
        bool mbQueueOwnershipTransfer = false;
    };

    /** Explicit buffer transition including expected state and pipeline domains. */
    struct FArdaRHIBufferTransitionDesc
    {
        EArdaRHIResourceState mStateBefore = EArdaRHIResourceState::Unknown;
        EArdaRHIResourceState mStateAfter = EArdaRHIResourceState::Unknown;
        EArdaRHIPipeline mSourcePipelines = EArdaRHIPipeline::Graphics;
        EArdaRHIPipeline mDestinationPipelines = EArdaRHIPipeline::Graphics;
        EArdaRHITransitionFlags mFlags = EArdaRHITransitionFlags::None;
        EArdaRHIQueueType mSourceQueue = EArdaRHIQueueType::Graphics;
        EArdaRHIQueueType mDestinationQueue = EArdaRHIQueueType::Graphics;
        bool mbQueueOwnershipTransfer = false;
    };

    /** Describes tiled texture coordinate. */
    struct FArdaRHITiledTextureCoordinate
    {
        /** Mip level containing the tile. */
        uint16_t mMipLevel = 0;
        /** Array level containing the tile. */
        uint16_t mArrayLevel = 0;
        /** Tile X coordinate. */
        uint32_t mX = 0;
        /** Tile Y coordinate. */
        uint32_t mY = 0;
        /** Tile Z coordinate. */
        uint32_t mZ = 0;
    };

    /** Describes tiled texture region. */
    struct FArdaRHITiledTextureRegion
    {
        /** Number of tiles in the region. */
        uint32_t mTileCount = 0;
        /** Region width in tiles. */
        uint32_t mWidth = 0;
        /** Region height in tiles. */
        uint32_t mHeight = 0;
        /** Region depth in tiles. */
        uint32_t mDepth = 0;
    };

    /** Describes packed mip desc. */
    struct FArdaRHIPackedMipDesc
    {
        /** Number of standard tiled mip levels. */
        uint32_t mStandardMipCount = 0;
        /** Number of packed mip levels. */
        uint32_t mPackedMipCount = 0;
        /** Number of tiles occupied by packed mips. */
        uint32_t mPackedMipTileCount = 0;
        /** First tile index used by packed mips. */
        uint32_t mStartTileIndex = 0;
    };

    /** Describes tile shape. */
    struct FArdaRHITileShape
    {
        /** Tile width in texels. */
        uint32_t mWidthInTexels = 0;
        /** Tile height in texels. */
        uint32_t mHeightInTexels = 0;
        /** Tile depth in texels. */
        uint32_t mDepthInTexels = 0;
    };

    /** Describes subresource tiling. */
    struct FArdaRHISubresourceTiling
    {
        /** Subresource width in tiles. */
        uint32_t mWidthInTiles = 0;
        /** Subresource height in tiles. */
        uint32_t mHeightInTiles = 0;
        /** Subresource depth in tiles. */
        uint32_t mDepthInTiles = 0;
        /** Stores the start tile index. */
        uint32_t mStartTileIndex = 0;
    };

    /** Describes sampler desc. */
    struct FArdaRHISamplerDesc
    {
        /** Stores the border color. */
        FArdaRHIColor mBorderColor{ 1.f, 1.f, 1.f, 1.f };
        /** Maximum anisotropy level. */
        float mMaxAnisotropy = 1.f;
        /** Mip-level-of-detail bias. */
        float mMipBias = 0.f;
        /** Whether minification filtering is enabled. */
        bool mbMinFilter = true;
        /** Whether magnification filtering is enabled. */
        bool mbMagFilter = true;
        /** Whether mip filtering is enabled. */
        bool mbMipFilter = true;
        /** Stores the address u. */
        EArdaRHISamplerAddressMode mAddressU = EArdaRHISamplerAddressMode::Clamp;
        /** Stores the address v. */
        EArdaRHISamplerAddressMode mAddressV = EArdaRHISamplerAddressMode::Clamp;
        /** Stores the address w. */
        EArdaRHISamplerAddressMode mAddressW = EArdaRHISamplerAddressMode::Clamp;
        /** Stores the reduction. */
        EArdaRHISamplerReduction mReduction = EArdaRHISamplerReduction::Standard;
        /** Stores the debug name. */
        eastl::string mDebugName;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHISamplerDesc& O) const noexcept;
    };

    /** Describes shader desc. */
    struct FArdaRHIShaderDesc
    {
        /** Stores the stage. */
        EArdaRHIShaderStage mStage = EArdaRHIShaderStage::None;
        /** Stores the bytecode. */
        const void* mBytecode = nullptr;
        /** Stores the bytecode size. */
        size_t mBytecodeSize = 0;
        /** Stores the entry point. */
        eastl::string mEntryPoint = "main";
        /** Stores the debug name. */
        eastl::string mDebugName;
    };

    /** Describes vertex attribute desc. */
    struct FArdaRHIVertexAttributeDesc
    {
        /** Stores the semantic name. */
        eastl::string mSemanticName;
        /** Stores the format. */
        EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
        /** Number of elements represented by the attribute. */
        uint32_t mArraySize = 1;
        /** Vertex-buffer binding index. */
        uint32_t mBufferIndex = 0;
        /** Byte offset within each vertex element. */
        uint32_t mOffset = 0;
        /** Vertex element stride in bytes. */
        uint32_t mElementStride = 0;
        /** Stores the instanced. */
        bool mbInstanced = false;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIVertexAttributeDesc& O) const noexcept
        {
            return mSemanticName == O.mSemanticName && mFormat == O.mFormat &&
                mArraySize == O.mArraySize && mBufferIndex == O.mBufferIndex &&
                mOffset == O.mOffset && mElementStride == O.mElementStride &&
                mbInstanced == O.mbInstanced;
        }
    };

    /** Describes binding layout item. */
    struct FArdaRHIBindingLayoutItem
    {
        /** Shader register slot. */
        uint32_t mSlot = 0;
        /** Number of array descriptors. */
        uint32_t mArraySize = 1;
        /** Stores the type. */
        EArdaRHIBindingType mType = EArdaRHIBindingType::TextureSRV;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIBindingLayoutItem& O) const noexcept { return mSlot == O.mSlot && mArraySize == O.mArraySize && mType == O.mType; }
    };

    /** Describes binding layout desc. */
    struct FArdaRHIBindingLayoutDesc
    {
        /** Stores the visibility. */
        EArdaRHIShaderStage mVisibility = EArdaRHIShaderStage::None;
        /** Stores the register space. */
        uint32_t mRegisterSpace = 0;
        /** Stores the register space is descriptor set. */
        bool mbRegisterSpaceIsDescriptorSet = false;
        /** Stores the items. */
        eastl::vector<FArdaRHIBindingLayoutItem> mItems;
        /** Stores the debug name. */
        eastl::string mDebugName;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIBindingLayoutDesc& O) const noexcept
        {
            return mVisibility == O.mVisibility && mRegisterSpace == O.mRegisterSpace &&
                mbRegisterSpaceIsDescriptorSet == O.mbRegisterSpaceIsDescriptorSet &&
                mItems == O.mItems;
        }
    };

    /** Describes framebuffer attachment. */
    struct FArdaRHIFramebufferAttachment
    {
        /** Stores the subresources. */
        FArdaRHITextureSubresourceRange mSubresources;
        /** Stores the format. */
        EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
        /** Stores the read only. */
        bool mbReadOnly = false;
    };

    /** Describes raster state. */
    struct FArdaRHIRasterState
    {
        /** Stores the fill mode. */
        EArdaRHIFillMode mFillMode = EArdaRHIFillMode::Solid;
        /** Stores the cull mode. */
        EArdaRHICullMode mCullMode = EArdaRHICullMode::Back;
        /** Stores the front counter clockwise. */
        bool mbFrontCounterClockwise = false;
        /** Stores the depth clip. */
        bool mbDepthClip = true;
        /** Stores the scissor. */
        bool mbScissor = false;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIRasterState& O) const noexcept
        {
            return mFillMode == O.mFillMode && mCullMode == O.mCullMode &&
                mbFrontCounterClockwise == O.mbFrontCounterClockwise &&
                mbDepthClip == O.mbDepthClip && mbScissor == O.mbScissor;
        }
    };
    /** Describes depth stencil state. */
    struct FArdaRHIDepthStencilState
    {
        /** Stores the depth test. */
        bool mbDepthTest = true;
        /** Stores the depth write. */
        bool mbDepthWrite = true;
        /** Stores the depth func. */
        EArdaRHIComparisonFunc mDepthFunc = EArdaRHIComparisonFunc::Less;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIDepthStencilState& O) const noexcept
        {
            return mbDepthTest == O.mbDepthTest && mbDepthWrite == O.mbDepthWrite &&
                mDepthFunc == O.mDepthFunc;
        }
    };
    /** Describes blend target state. */
    struct FArdaRHIBlendTargetState
    {
        /** Stores the enable. */
        bool mbEnable = false;
        /** Stores the source color. */
        EArdaRHIBlendFactor mSourceColor = EArdaRHIBlendFactor::One;
        /** Stores the destination color. */
        EArdaRHIBlendFactor mDestinationColor = EArdaRHIBlendFactor::Zero;
        /** Stores the source alpha. */
        EArdaRHIBlendFactor mSourceAlpha = EArdaRHIBlendFactor::One;
        /** Stores the destination alpha. */
        EArdaRHIBlendFactor mDestinationAlpha = EArdaRHIBlendFactor::Zero;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIBlendTargetState& O) const noexcept
        {
            return mbEnable == O.mbEnable && mSourceColor == O.mSourceColor &&
                mDestinationColor == O.mDestinationColor &&
                mSourceAlpha == O.mSourceAlpha &&
                mDestinationAlpha == O.mDestinationAlpha;
        }
    };
    /** Describes blend state. */
    struct FArdaRHIBlendState
    {
        /** Stores the targets. */
        FArdaRHIBlendTargetState mTargets[ArdaRHIMaxRenderTargets]{};
        /** Stores the alpha to coverage. */
        bool mbAlphaToCoverage = false;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIBlendState& O) const noexcept
        {
            if (mbAlphaToCoverage != O.mbAlphaToCoverage) return false;
            for (uint32_t I = 0; I < ArdaRHIMaxRenderTargets; ++I)
                if (!(mTargets[I] == O.mTargets[I])) return false;
            return true;
        }
    };
    /** Describes viewport. */
    struct FArdaRHIViewport
    {
        /** Minimum X coordinate. */
        float mMinX = 0.f;
        /** Maximum X coordinate. */
        float mMaxX = 0.f;
        /** Minimum Y coordinate. */
        float mMinY = 0.f;
        /** Maximum Y coordinate. */
        float mMaxY = 0.f;
        /** Minimum depth value. */
        float mMinZ = 0.f;
        /** Maximum depth value. */
        float mMaxZ = 1.f;
    };
    /** Describes rect. */
    struct FArdaRHIRect
    {
        /** Minimum X coordinate. */
        int32_t mMinX = 0;
        /** Maximum X coordinate. */
        int32_t mMaxX = 0;
        /** Minimum Y coordinate. */
        int32_t mMinY = 0;
        /** Maximum Y coordinate. */
        int32_t mMaxY = 0;
    };
    /** Describes draw arguments. */
    struct FArdaRHIDrawArguments
    {
        /** Number of vertices or indices to draw. */
        uint32_t mVertexCount = 0;
        /** Number of instances to draw. */
        uint32_t mInstanceCount = 1;
        /** First index for indexed draws. */
        uint32_t mStartIndex = 0;
        /** First vertex or base-vertex offset. */
        uint32_t mStartVertex = 0;
        /** First instance identifier. */
        uint32_t mStartInstance = 0;
    };

    /** Describes format info. */
    struct FArdaRHIFormatInfo
    {
        /** Stores the depth. */
        bool mbDepth = false;
        /** Stores the stencil. */
        bool mbStencil = false;
        /** Stores the integer. */
        bool mbInteger = false;
        /** Bytes occupied by one texel or one compressed block. */
        uint32_t mBytesPerBlock = 0;
        /** Width in texels of one storage block. */
        uint32_t mBlockWidth = 1;
        /** Height in texels of one storage block. */
        uint32_t mBlockHeight = 1;
    };

    /** Describes memory requirements. */
    struct FArdaRHIMemoryRequirements
    {
        /** Stores the size. */
        uint64_t mSize = 0;
        /** Stores the alignment. */
        uint64_t mAlignment = 0;
        /** Backend memory-type compatibility mask (all bits for APIs without memory types). */
        uint32_t mMemoryTypeBits = 0xffffffffu;
    };

    /**
     * Returns the arda rhiformat info.
     * @param Format The format.
     * @return A reference to the requested value.
     */
    [[nodiscard]] const FArdaRHIFormatInfo& GetArdaRHIFormatInfo(EArdaRHIFormat Format) noexcept;
    /** @return The byte size of one uncompressed format element, or zero for compressed/unknown formats. */
    [[nodiscard]] uint32_t GetArdaRHIFormatElementSize(
        EArdaRHIFormat Format) noexcept;
    /** @return The number of independently addressable format planes. */
    [[nodiscard]] uint32_t GetArdaRHIFormatPlaneCount(
        EArdaRHIFormat Format) noexcept;
    /** @return One dimension of a texture at the requested mip level. */
    [[nodiscard]] uint32_t GetArdaRHITextureMipExtent(
        uint32_t BaseExtent, uint32_t MipLevel) noexcept;
    /**
     * Validates matching texture slices and resolves sentinel source extents.
     * @return Success and a concrete non-empty copy extent, or a validation error.
     */
    [[nodiscard]] FArdaRHIStatus ResolveArdaRHITextureCopyExtent(
        const FArdaRHITextureDesc& DestinationDesc,
        const FArdaRHITextureSlice& DestinationSlice,
        const FArdaRHITextureDesc& SourceDesc,
        const FArdaRHITextureSlice& SourceSlice,
        FArdaRHITextureCopyExtent& OutExtent) noexcept;
    /**
     * Validates a whole-subresource multisample resolve and returns its extent.
     * Slice width, height, and depth are ignored because resolves are not regions.
     */
    [[nodiscard]] FArdaRHIStatus ValidateArdaRHITextureResolve(
        const FArdaRHITextureDesc& DestinationDesc,
        const FArdaRHITextureSlice& DestinationSlice,
        const FArdaRHITextureDesc& SourceDesc,
        const FArdaRHITextureSlice& SourceSlice,
        FArdaRHITextureCopyExtent& OutExtent) noexcept;

    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHITextureSubresourceRange& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIBufferRange& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHITextureDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIBufferDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIViewDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHISamplerDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIVertexAttributeDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIBindingLayoutDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIRasterState& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIDepthStencilState& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIBlendTargetState& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIBlendState& Value) noexcept;

    /**
     * Central descriptor validation used before cache lookup and native creation.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHITextureDesc& Value) noexcept;
    /**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIBufferDesc& Value) noexcept;
    /**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIViewDesc& Value) noexcept;
    /**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHISamplerDesc& Value) noexcept;
    /**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIVertexAttributeDesc& Value) noexcept;
    /**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIBindingLayoutDesc& Value) noexcept;
}
