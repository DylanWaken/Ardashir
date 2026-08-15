#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace arda::rhi
{
    struct FArdaRHITextureDesc;
    struct FArdaRHIBufferDesc;

    inline constexpr uint32_t ArdaRHIAllSubresources = std::numeric_limits<uint32_t>::max();
    inline constexpr uint64_t ArdaRHIWholeBuffer = std::numeric_limits<uint64_t>::max();
    inline constexpr uint32_t ArdaRHIMaxRenderTargets = 8;

    enum class EArdaRHIResult : uint8_t
    {
        Success,
        InvalidArgument,
        Unsupported,
        BackendFailure,
        InvalidState,
        WrongDevice
    };

    struct FArdaRHIStatus
    {
        EArdaRHIResult mCode = EArdaRHIResult::Success;
        eastl::string mMessage;

        [[nodiscard]] bool IsSuccess() const noexcept { return mCode == EArdaRHIResult::Success; }
        [[nodiscard]] explicit operator bool() const noexcept { return IsSuccess(); }
        static FArdaRHIStatus Success() { return {}; }
        static FArdaRHIStatus Error(EArdaRHIResult Code, const char* Message)
        {
            return { Code, Message ? Message : "" };
        }
    };

    template <typename T>
    struct TArdaRHIResult
    {
        T mValue{};
        FArdaRHIStatus mStatus;
        [[nodiscard]] explicit operator bool() const noexcept { return mStatus.IsSuccess(); }
    };

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
        BC5UNorm, BC5SNorm, BC6HUFloat, BC6HSFloat, BC7UNorm, BC7UNormSRGB
    };

    enum class EArdaRHITextureDimension : uint8_t
    {
        Unknown, Texture1D, Texture1DArray, Texture2D, Texture2DArray,
        TextureCube, TextureCubeArray, Texture2DMS, Texture2DMSArray, Texture3D
    };

    enum class EArdaRHICpuAccess : uint8_t { None, Read, Write };
    enum class EArdaRHIQueueType : uint8_t { Graphics, Compute, Copy };
    enum class EArdaRHIHeapType : uint8_t { DeviceLocal, Upload, Readback };
    enum class EArdaRHIBindlessLayoutType : uint8_t
    {
        Immutable, MutableSrvUavCbv, MutableCounters, MutableSampler
    };
    /** Identifies the backend representation of an imported native resource. */
    enum class EArdaRHINativeResourceType : uint8_t
    {
        D3D12Resource,
        VulkanImage,
        VulkanBuffer
    };
    /** Controls whether an imported native resource remains caller-owned. */
    enum class EArdaRHINativeOwnership : uint8_t
    {
        Borrowed,
        Transferred
    };
    enum class EArdaRHISamplerFeedbackFormat : uint8_t
    {
        MinMipOpaque, MipRegionUsedOpaque
    };
    enum class EArdaRHIRayTracingGeometryType : uint8_t { Triangles, AABBs };
    enum class EArdaRHIOpacityMicromapFormat : uint8_t { TwoState = 1, FourState = 2 };
    enum class EArdaRHIShaderStage : uint16_t
    {
        None = 0, Vertex = 1u << 0, Hull = 1u << 1, Domain = 1u << 2,
        Geometry = 1u << 3, Pixel = 1u << 4, Compute = 1u << 5,
        Amplification = 1u << 6, Mesh = 1u << 7,
        RayGeneration = 1u << 8, AnyHit = 1u << 9,
        ClosestHit = 1u << 10, Miss = 1u << 11,
        Intersection = 1u << 12, Callable = 1u << 13,
        AllGraphics = 0x00df, AllRayTracing = 0x3f00, All = 0x3fff
    };

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
        OpacityMicromapWrite = 1u << 21, OpacityMicromapBuildInput = 1u << 22
    };

    enum class EArdaRHITextureUsage : uint16_t
    {
        None = 0, ShaderResource = 1u << 0, UnorderedAccess = 1u << 1,
        RenderTarget = 1u << 2, DepthStencil = 1u << 3,
        Typeless = 1u << 4
    };

    enum class EArdaRHIBufferUsage : uint16_t
    {
        None = 0, ShaderResource = 1u << 0, UnorderedAccess = 1u << 1,
        Vertex = 1u << 2, Index = 1u << 3, Constant = 1u << 4,
        Indirect = 1u << 5, Raw = 1u << 6, Structured = 1u << 7,
        Volatile = 1u << 8, AccelStructBuildInput = 1u << 9,
        AccelStructStorage = 1u << 10, ShaderBindingTable = 1u << 11
    };

    enum class EArdaRHIRayTracingGeometryFlags : uint8_t
    {
        None = 0, Opaque = 1u << 0, NoDuplicateAnyHitInvocation = 1u << 1
    };
    enum class EArdaRHIAccelStructBuildFlags : uint8_t
    {
        None = 0, AllowUpdate = 1u << 0, AllowCompaction = 1u << 1,
        PreferFastTrace = 1u << 2, PreferFastBuild = 1u << 3,
        MinimizeMemory = 1u << 4, PerformUpdate = 1u << 5,
        AllowEmptyInstances = 1u << 7
    };
    enum class EArdaRHIOpacityMicromapBuildFlags : uint8_t
    {
        None = 0, FastTrace = 1u << 0, FastBuild = 1u << 1,
        AllowCompaction = 1u << 2
    };

#define ARDA_RHI_FLAG_OPERATORS(Type) \
    constexpr Type operator|(Type A, Type B) noexcept { return static_cast<Type>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B)); } \
    constexpr Type operator&(Type A, Type B) noexcept { return static_cast<Type>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B)); } \
    constexpr Type& operator|=(Type& A, Type B) noexcept { A = A | B; return A; } \
    constexpr bool HasAnyFlags(Type Value, Type Flags) noexcept { return static_cast<uint32_t>(Value & Flags) != 0; }

    ARDA_RHI_FLAG_OPERATORS(EArdaRHIShaderStage)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIResourceState)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHITextureUsage)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIBufferUsage)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIRayTracingGeometryFlags)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIAccelStructBuildFlags)
    ARDA_RHI_FLAG_OPERATORS(EArdaRHIOpacityMicromapBuildFlags)
#undef ARDA_RHI_FLAG_OPERATORS

    enum class EArdaRHISamplerAddressMode : uint8_t { Clamp, Wrap, Border, Mirror, MirrorOnce };
    enum class EArdaRHISamplerReduction : uint8_t { Standard, Comparison, Minimum, Maximum };
    enum class EArdaRHIPrimitiveTopology : uint8_t { PointList, LineList, LineStrip, TriangleList, TriangleStrip, PatchList };
    enum class EArdaRHIFillMode : uint8_t { Solid, Wireframe };
    enum class EArdaRHICullMode : uint8_t { Back, Front, None };
    enum class EArdaRHIComparisonFunc : uint8_t { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };
    enum class EArdaRHIBlendFactor : uint8_t
    {
        Zero, One, SourceColor, InverseSourceColor, SourceAlpha,
        InverseSourceAlpha, DestinationAlpha, InverseDestinationAlpha,
        DestinationColor, InverseDestinationColor
    };
    enum class EArdaRHIBindingType : uint8_t
    {
        TextureSRV, TextureUAV, TypedBufferSRV, TypedBufferUAV,
        StructuredBufferSRV, StructuredBufferUAV, RawBufferSRV, RawBufferUAV,
        ConstantBuffer, VolatileConstantBuffer, Sampler, PushConstants,
        RayTracingAccelStruct, SamplerFeedbackTextureUAV
    };

    struct FArdaRHIColor
    {
        float mR = 0.f, mG = 0.f, mB = 0.f, mA = 0.f;
        bool operator==(const FArdaRHIColor& O) const noexcept { return mR == O.mR && mG == O.mG && mB == O.mB && mA == O.mA; }
    };

    struct FArdaRHITextureSubresourceRange
    {
        uint32_t mBaseMipLevel = 0;
        uint32_t mMipLevelCount = ArdaRHIAllSubresources;
        uint32_t mBaseArraySlice = 0;
        uint32_t mArraySliceCount = ArdaRHIAllSubresources;
        bool operator==(const FArdaRHITextureSubresourceRange& O) const noexcept
        {
            return mBaseMipLevel == O.mBaseMipLevel && mMipLevelCount == O.mMipLevelCount &&
                mBaseArraySlice == O.mBaseArraySlice && mArraySliceCount == O.mArraySliceCount;
        }
        [[nodiscard]] FArdaRHITextureSubresourceRange Resolve(
            const FArdaRHITextureDesc& Desc) const noexcept;
    };

    struct FArdaRHIBufferRange
    {
        uint64_t mByteOffset = 0;
        uint64_t mByteSize = ArdaRHIWholeBuffer;
        bool operator==(const FArdaRHIBufferRange& O) const noexcept { return mByteOffset == O.mByteOffset && mByteSize == O.mByteSize; }
        [[nodiscard]] FArdaRHIBufferRange Resolve(const FArdaRHIBufferDesc& Desc) const noexcept;
        [[nodiscard]] bool IsWholeBuffer(const FArdaRHIBufferDesc& Desc) const noexcept;
    };

    struct FArdaRHITextureDesc
    {
        uint32_t mWidth = 1, mHeight = 1, mDepth = 1, mArraySize = 1, mMipLevels = 1, mSampleCount = 1;
        EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
        EArdaRHITextureDimension mDimension = EArdaRHITextureDimension::Texture2D;
        EArdaRHITextureUsage mUsage = EArdaRHITextureUsage::ShaderResource;
        EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
        bool mbKeepInitialState = false;
        bool mbVirtual = false;
        bool mbTiled = false;
        FArdaRHIColor mClearValue;
        bool mbUseClearValue = false;
        eastl::string mDebugName;
        bool operator==(const FArdaRHITextureDesc& O) const noexcept;
    };

    struct FArdaRHIBufferDesc
    {
        uint64_t mByteSize = 0;
        uint32_t mStructureStride = 0, mMaxVersions = 0;
        EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
        EArdaRHIBufferUsage mUsage = EArdaRHIBufferUsage::None;
        EArdaRHICpuAccess mCpuAccess = EArdaRHICpuAccess::None;
        EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Common;
        bool mbKeepInitialState = false;
        bool mbVirtual = false;
        eastl::string mDebugName;
        bool operator==(const FArdaRHIBufferDesc& O) const noexcept;
    };

    struct FArdaRHIViewDesc
    {
        EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
        EArdaRHITextureDimension mDimension = EArdaRHITextureDimension::Unknown;
        FArdaRHITextureSubresourceRange mTextureRange;
        FArdaRHIBufferRange mBufferRange;
        bool operator==(const FArdaRHIViewDesc& O) const noexcept
        {
            return mFormat == O.mFormat && mDimension == O.mDimension &&
                mTextureRange == O.mTextureRange && mBufferRange == O.mBufferRange;
        }
    };

    /** Backend-neutral description of a native texture import. */
    struct FArdaRHINativeTextureImportDesc
    {
        uintptr_t mNativeObject = 0;
        EArdaRHINativeResourceType mNativeType = EArdaRHINativeResourceType::D3D12Resource;
        EArdaRHINativeOwnership mOwnership = EArdaRHINativeOwnership::Borrowed;
        FArdaRHITextureDesc mTexture;
        EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
        bool operator==(const FArdaRHINativeTextureImportDesc& O) const noexcept
        {
            return mNativeObject == O.mNativeObject && mNativeType == O.mNativeType &&
                mOwnership == O.mOwnership && mTexture == O.mTexture &&
                mInitialState == O.mInitialState;
        }
    };

    /** Backend-neutral description of a native buffer import. */
    struct FArdaRHINativeBufferImportDesc
    {
        uintptr_t mNativeObject = 0;
        EArdaRHINativeResourceType mNativeType = EArdaRHINativeResourceType::D3D12Resource;
        EArdaRHINativeOwnership mOwnership = EArdaRHINativeOwnership::Borrowed;
        FArdaRHIBufferDesc mBuffer;
        EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
        bool operator==(const FArdaRHINativeBufferImportDesc& O) const noexcept
        {
            return mNativeObject == O.mNativeObject && mNativeType == O.mNativeType &&
                mOwnership == O.mOwnership && mBuffer == O.mBuffer &&
                mInitialState == O.mInitialState;
        }
    };

    struct FArdaRHITextureSlice
    {
        uint32_t mX = 0, mY = 0, mZ = 0;
        uint32_t mWidth = ArdaRHIAllSubresources;
        uint32_t mHeight = ArdaRHIAllSubresources;
        uint32_t mDepth = ArdaRHIAllSubresources;
        uint32_t mMipLevel = 0, mArraySlice = 0;
    };

    struct FArdaRHITiledTextureCoordinate
    {
        uint16_t mMipLevel = 0, mArrayLevel = 0;
        uint32_t mX = 0, mY = 0, mZ = 0;
    };

    struct FArdaRHITiledTextureRegion
    {
        uint32_t mTileCount = 0, mWidth = 0, mHeight = 0, mDepth = 0;
    };

    struct FArdaRHIPackedMipDesc
    {
        uint32_t mStandardMipCount = 0, mPackedMipCount = 0;
        uint32_t mPackedMipTileCount = 0, mStartTileIndex = 0;
    };

    struct FArdaRHITileShape
    {
        uint32_t mWidthInTexels = 0, mHeightInTexels = 0, mDepthInTexels = 0;
    };

    struct FArdaRHISubresourceTiling
    {
        uint32_t mWidthInTiles = 0, mHeightInTiles = 0, mDepthInTiles = 0;
        uint32_t mStartTileIndex = 0;
    };

    struct FArdaRHISamplerDesc
    {
        FArdaRHIColor mBorderColor{ 1.f, 1.f, 1.f, 1.f };
        float mMaxAnisotropy = 1.f, mMipBias = 0.f;
        bool mbMinFilter = true, mbMagFilter = true, mbMipFilter = true;
        EArdaRHISamplerAddressMode mAddressU = EArdaRHISamplerAddressMode::Clamp;
        EArdaRHISamplerAddressMode mAddressV = EArdaRHISamplerAddressMode::Clamp;
        EArdaRHISamplerAddressMode mAddressW = EArdaRHISamplerAddressMode::Clamp;
        EArdaRHISamplerReduction mReduction = EArdaRHISamplerReduction::Standard;
        eastl::string mDebugName;
        bool operator==(const FArdaRHISamplerDesc& O) const noexcept;
    };

    struct FArdaRHIShaderDesc
    {
        EArdaRHIShaderStage mStage = EArdaRHIShaderStage::None;
        const void* mBytecode = nullptr;
        size_t mBytecodeSize = 0;
        eastl::string mEntryPoint = "main";
        eastl::string mDebugName;
    };

    struct FArdaRHIVertexAttributeDesc
    {
        eastl::string mSemanticName;
        EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
        uint32_t mArraySize = 1, mBufferIndex = 0, mOffset = 0, mElementStride = 0;
        bool mbInstanced = false;
        bool operator==(const FArdaRHIVertexAttributeDesc& O) const noexcept
        {
            return mSemanticName == O.mSemanticName && mFormat == O.mFormat &&
                mArraySize == O.mArraySize && mBufferIndex == O.mBufferIndex &&
                mOffset == O.mOffset && mElementStride == O.mElementStride &&
                mbInstanced == O.mbInstanced;
        }
    };

    struct FArdaRHIBindingLayoutItem
    {
        uint32_t mSlot = 0, mArraySize = 1;
        EArdaRHIBindingType mType = EArdaRHIBindingType::TextureSRV;
        bool operator==(const FArdaRHIBindingLayoutItem& O) const noexcept { return mSlot == O.mSlot && mArraySize == O.mArraySize && mType == O.mType; }
    };

    struct FArdaRHIBindingLayoutDesc
    {
        EArdaRHIShaderStage mVisibility = EArdaRHIShaderStage::None;
        uint32_t mRegisterSpace = 0;
        bool mbRegisterSpaceIsDescriptorSet = false;
        eastl::vector<FArdaRHIBindingLayoutItem> mItems;
        eastl::string mDebugName;
        bool operator==(const FArdaRHIBindingLayoutDesc& O) const noexcept
        {
            return mVisibility == O.mVisibility && mRegisterSpace == O.mRegisterSpace &&
                mbRegisterSpaceIsDescriptorSet == O.mbRegisterSpaceIsDescriptorSet &&
                mItems == O.mItems;
        }
    };

    struct FArdaRHIFramebufferAttachment
    {
        FArdaRHITextureSubresourceRange mSubresources;
        EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
        bool mbReadOnly = false;
    };

    struct FArdaRHIRasterState
    {
        EArdaRHIFillMode mFillMode = EArdaRHIFillMode::Solid;
        EArdaRHICullMode mCullMode = EArdaRHICullMode::Back;
        bool mbFrontCounterClockwise = false;
        bool mbDepthClip = true;
        bool mbScissor = false;
        bool operator==(const FArdaRHIRasterState& O) const noexcept
        {
            return mFillMode == O.mFillMode && mCullMode == O.mCullMode &&
                mbFrontCounterClockwise == O.mbFrontCounterClockwise &&
                mbDepthClip == O.mbDepthClip && mbScissor == O.mbScissor;
        }
    };
    struct FArdaRHIDepthStencilState
    {
        bool mbDepthTest = true;
        bool mbDepthWrite = true;
        EArdaRHIComparisonFunc mDepthFunc = EArdaRHIComparisonFunc::Less;
        bool operator==(const FArdaRHIDepthStencilState& O) const noexcept
        {
            return mbDepthTest == O.mbDepthTest && mbDepthWrite == O.mbDepthWrite &&
                mDepthFunc == O.mDepthFunc;
        }
    };
    struct FArdaRHIBlendTargetState
    {
        bool mbEnable = false;
        EArdaRHIBlendFactor mSourceColor = EArdaRHIBlendFactor::One;
        EArdaRHIBlendFactor mDestinationColor = EArdaRHIBlendFactor::Zero;
        EArdaRHIBlendFactor mSourceAlpha = EArdaRHIBlendFactor::One;
        EArdaRHIBlendFactor mDestinationAlpha = EArdaRHIBlendFactor::Zero;
        bool operator==(const FArdaRHIBlendTargetState& O) const noexcept
        {
            return mbEnable == O.mbEnable && mSourceColor == O.mSourceColor &&
                mDestinationColor == O.mDestinationColor &&
                mSourceAlpha == O.mSourceAlpha &&
                mDestinationAlpha == O.mDestinationAlpha;
        }
    };
    struct FArdaRHIBlendState
    {
        FArdaRHIBlendTargetState mTargets[ArdaRHIMaxRenderTargets]{};
        bool mbAlphaToCoverage = false;
        bool operator==(const FArdaRHIBlendState& O) const noexcept
        {
            if (mbAlphaToCoverage != O.mbAlphaToCoverage) return false;
            for (uint32_t I = 0; I < ArdaRHIMaxRenderTargets; ++I)
                if (!(mTargets[I] == O.mTargets[I])) return false;
            return true;
        }
    };
    struct FArdaRHIViewport { float mMinX = 0.f, mMaxX = 0.f, mMinY = 0.f, mMaxY = 0.f, mMinZ = 0.f, mMaxZ = 1.f; };
    struct FArdaRHIRect { int32_t mMinX = 0, mMaxX = 0, mMinY = 0, mMaxY = 0; };
    struct FArdaRHIDrawArguments { uint32_t mVertexCount = 0, mInstanceCount = 1, mStartIndex = 0, mStartVertex = 0, mStartInstance = 0; };

    struct FArdaRHIFormatInfo
    {
        bool mbDepth = false;
        bool mbStencil = false;
        bool mbInteger = false;
    };

    struct FArdaRHIMemoryRequirements
    {
        uint64_t mSize = 0;
        uint64_t mAlignment = 0;
    };

    [[nodiscard]] const FArdaRHIFormatInfo& GetArdaRHIFormatInfo(EArdaRHIFormat Format) noexcept;

    [[nodiscard]] size_t HashValue(const FArdaRHITextureSubresourceRange& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIBufferRange& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHITextureDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIBufferDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIViewDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHISamplerDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIVertexAttributeDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIBindingLayoutDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIRasterState& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIDepthStencilState& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIBlendTargetState& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIBlendState& Value) noexcept;

    /** Central descriptor validation used before cache lookup and native creation. */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHITextureDesc& Value) noexcept;
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIBufferDesc& Value) noexcept;
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIViewDesc& Value) noexcept;
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHISamplerDesc& Value) noexcept;
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIVertexAttributeDesc& Value) noexcept;
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIBindingLayoutDesc& Value) noexcept;
}
