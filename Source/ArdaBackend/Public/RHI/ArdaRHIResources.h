/** @file ArdaRHIResources.h
 * Declares RHI resource interfaces, descriptors, pipeline state, and validation helpers.
 */

#pragma once

#include "ArdaRHIRef.h"
#include "ArdaRHIResource.h"
#include "ArdaRHITypes.h"

#include <EASTL/vector.h>

namespace arda::rhi
{
    /** Interface for texture. */
    class IArdaRHITexture : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHITextureDesc& GetDesc() const noexcept = 0;
        /** Returns the physical identity. */
        [[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
    };

    /** Mutable logical indirection to a texture; the referenced texture is retained. */
    class IArdaRHITextureReference : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the texture.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHITextureRef& GetTexture() const noexcept = 0;
    };

    /** Interface for buffer. */
    class IArdaRHIBuffer : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIBufferDesc& GetDesc() const noexcept = 0;
        /** Returns the physical identity. */
        [[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
    };

    /** Describes uniform buffer desc. */
    struct FArdaRHIUniformBufferDesc
    {
        /** Stores the byte size. */
        size_t mByteSize = 0;
        /** Stores the max versions. */
        uint32_t mMaxVersions = 1;
        /** Stores the debug name. */
        eastl::string mDebugName;
    };

    /** Constant-buffer resource with a stable logical identity. */
    class IArdaRHIUniformBuffer : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIUniformBufferDesc& GetDesc() const noexcept = 0;
        /**
         * Returns the buffer.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIBufferRef& GetBuffer() const noexcept = 0;
    };

    /** Describes heap desc. */
    struct FArdaRHIHeapDesc
    {
        /** Stores the capacity. */
        uint64_t mCapacity = 0;
        /** Stores the type. */
        EArdaRHIHeapType mType = EArdaRHIHeapType::DeviceLocal;
        /** Compatible backend memory types, normally copied/intersected from requirements. */
        uint32_t mMemoryTypeBits = 0xffffffffu;
        /** Stores the debug name. */
        eastl::string mDebugName;
    };

    /** Interface for heap. */
    class IArdaRHIHeap : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIHeapDesc& GetDesc() const noexcept = 0;
    };

    /** Describes staging texture desc. */
    struct FArdaRHIStagingTextureDesc
    {
        /** Stores the texture. */
        FArdaRHITextureDesc mTexture;
        /** Stores the CPU access. */
        EArdaRHICpuAccess mCpuAccess = EArdaRHICpuAccess::Read;
        /** Stores the debug name. */
        eastl::string mDebugName;
    };

    /** Interface for staging texture. */
    class IArdaRHIStagingTexture : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIStagingTextureDesc& GetDesc() const noexcept = 0;
    };

    /** Interface for event query. */
    class IArdaRHIEventQuery : public virtual IArdaRHIResource {};
    /** Interface for timer query. */
    class IArdaRHITimerQuery : public virtual IArdaRHIResource {};
    /** Queue fence facade backed by an event query; one signal is tracked at a time. */
    class IArdaRHIGpuFence : public virtual IArdaRHIResource {};

    /** Interface for shader resource view. */
    class IArdaRHIShaderResourceView : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the resource.
         * @return The requested object pointer.
         */
        [[nodiscard]] virtual IArdaRHIResource* GetResource() const noexcept = 0;
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIViewDesc& GetDesc() const noexcept = 0;
    };

    /** Interface for unordered access view. */
    class IArdaRHIUnorderedAccessView : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the resource.
         * @return The requested object pointer.
         */
        [[nodiscard]] virtual IArdaRHIResource* GetResource() const noexcept = 0;
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIViewDesc& GetDesc() const noexcept = 0;
    };

    /** Interface for sampler. */
    class IArdaRHISampler : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHISamplerDesc& GetDesc() const noexcept = 0;
    };

    /** Interface for shader. */
    class IArdaRHIShader : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the stage.
         * @return The requested value.
         */
        [[nodiscard]] virtual EArdaRHIShaderStage GetStage() const noexcept = 0;
        /**
         * Returns a deterministic identity derived from bytecode, stage, and
         * entry point for persistent pipeline-cache keys.
         */
        [[nodiscard]] virtual uint64_t GetPersistentCacheHash() const noexcept { return 0; }
    };

    /** Interface for shader library. */
    class IArdaRHIShaderLibrary : public virtual IArdaRHIResource {};

    /** Complete deterministic key used to cache input layouts. */
    struct FArdaRHIInputLayoutDesc
    {
        /** Stores the attributes. */
        eastl::vector<FArdaRHIVertexAttributeDesc> mAttributes;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIInputLayoutDesc& O) const noexcept
        {
            return mAttributes == O.mAttributes;
        }
    };

    /** Interface for input layout. */
    class IArdaRHIInputLayout : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIInputLayoutDesc& GetDesc() const noexcept = 0;
    };
    /** Interface for binding layout. */
    class IArdaRHIBindingLayout : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIBindingLayoutDesc& GetDesc() const noexcept = 0;
    };

    /** Describes binding item. */
    struct FArdaRHIBindingItem
    {
        /** Stores the slot. */
        uint32_t mSlot = 0;
        /** Stores the array element. */
        uint32_t mArrayElement = 0;
        /** Stores the type. */
        EArdaRHIBindingType mType = EArdaRHIBindingType::TextureSRV;
        /** Stores the resource. */
        TArdaRHIRef<IArdaRHIResource> mResource;
        /** Stores the view. */
        FArdaRHIViewDesc mView;
    };

    /** Describes binding set desc. */
    struct FArdaRHIBindingSetDesc
    {
        /** Stores the layout. */
        FArdaRHIBindingLayoutRef mLayout;
        /** Stores the items. */
        eastl::vector<FArdaRHIBindingItem> mItems;
        /** Actual count allocated for a variable-count bindless binding. */
        uint32_t mVariableDescriptorCount = 0;
        /** Stores the debug name. */
        eastl::string mDebugName;
    };

    /** Interface for binding set. */
    class IArdaRHIBindingSet : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIBindingSetDesc& GetDesc() const noexcept = 0;
    };

    /** Describes bindless layout desc. */
    struct FArdaRHIBindlessLayoutDesc
    {
        /** Stores the visibility. */
        EArdaRHIShaderStage mVisibility = EArdaRHIShaderStage::None;
        /** Stores the first slot. */
        uint32_t mFirstSlot = 0;
        /** Native descriptor-set/register-space selected for this table. */
        uint32_t mRegisterSpace = 0;
        /** Stores the max capacity. */
        uint32_t mMaxCapacity = 0;
        /** Zero-capacity layouts request the backend's maximum runtime array. */
        bool mbUnbounded = false;
        /** Descriptors may be changed after a table has been bound. */
        bool mbUpdateAfterBind = false;
        /** The last native binding uses the table's actual descriptor count. */
        bool mbVariableDescriptorCount = false;
        /** Shaders directly index the native resource/sampler heap. */
        bool mbDirectHeapIndexing = false;
        /** Stores the layout type. */
        EArdaRHIBindlessLayoutType mLayoutType = EArdaRHIBindlessLayoutType::Immutable;
        /** Stores the register spaces. */
        eastl::vector<FArdaRHIBindingLayoutItem> mRegisterSpaces;
        /** Stores the debug name. */
        eastl::string mDebugName;
        /** Deprecated compatibility flag; descriptor-table versions retain their resources. */
        bool mbAllowUnsafeDescriptorTableLifetime = false;
    };

    /**
     * Mutable bounded descriptor table. Each native table version retains its
     * written resources through all command-list uses of that version.
     */
    class IArdaRHIDescriptorTable : public virtual IArdaRHIBindingSet
    {
    public:
        /**
         * Returns the capacity.
         * @return The requested numeric value.
         */
        [[nodiscard]] virtual uint32_t GetCapacity() const noexcept = 0;
        /**
         * Returns the first descriptor index in heap.
         * @return The requested numeric value.
         */
        [[nodiscard]] virtual uint32_t GetFirstDescriptorIndexInHeap() const noexcept = 0;
    };

    /** Kind of resource retained by a general resource collection. */
    enum class EArdaRHIResourceCollectionItemType : uint8_t
    {
        Texture,
        TextureReference,
        Buffer,
        ShaderResourceView,
        UnorderedAccessView,
        AccelerationStructure,
        Sampler
    };

    /** One typed member of a general resource collection. */
    struct FArdaRHIResourceCollectionItem
    {
        EArdaRHIResourceCollectionItemType mType =
            EArdaRHIResourceCollectionItemType::Texture;
        FArdaRHITextureRef mTexture;
        FArdaRHITextureReferenceRef mTextureReference;
        FArdaRHIBufferRef mBuffer;
        FArdaRHIShaderResourceViewRef mShaderResourceView;
        FArdaRHIUnorderedAccessViewRef mUnorderedAccessView;
        FArdaRHIAccelStructRef mAccelerationStructure;
        FArdaRHISamplerRef mSampler;
    };

    /** Mutable collection used by bindless and ray/ML systems. */
    struct FArdaRHIResourceCollectionDesc
    {
        eastl::vector<FArdaRHIResourceCollectionItem> mItems;
        bool mbMutable = false;
        bool mbDirectlyIndexed = false;
        eastl::string mDebugName;
    };

    /** General resource collection with an optional native bindless base. */
    class IArdaRHIResourceCollection : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIResourceCollectionDesc&
            GetDesc() const noexcept = 0;
        [[nodiscard]] virtual uint32_t GetFirstDescriptorIndexInHeap()
            const noexcept = 0;
    };

    /** Describes framebuffer target. */
    struct FArdaRHIFramebufferTarget
    {
        /** Stores the texture. */
        FArdaRHITextureRef mTexture;
        /** Stores the attachment. */
        FArdaRHIFramebufferAttachment mAttachment;
    };

    /** Describes framebuffer desc. */
    struct FArdaRHIFramebufferDesc
    {
        /** Stores the color attachments. */
        eastl::vector<FArdaRHIFramebufferTarget> mColorAttachments;
        /** Stores the depth attachment. */
        FArdaRHIFramebufferTarget mDepthAttachment;
        /** Stores the debug name. */
        eastl::string mDebugName;
    };

    /** Interface for framebuffer. */
    class IArdaRHIFramebuffer : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIFramebufferDesc& GetDesc() const noexcept = 0;
    };

    /** Describes graphics pipeline desc. */
    struct FArdaRHIGraphicsPipelineDesc
    {
        /** Stores the topology. */
        EArdaRHIPrimitiveTopology mTopology = EArdaRHIPrimitiveTopology::TriangleList;
        /** Stores the patch control points. */
        uint32_t mPatchControlPoints = 0;
        /** Stores the input layout. */
        FArdaRHIInputLayoutRef mInputLayout;
        /** Vertex shader used by the pipeline. */
        FArdaRHIShaderRef mVertexShader;
        /** Hull shader used by the pipeline. */
        FArdaRHIShaderRef mHullShader;
        /** Domain shader used by the pipeline. */
        FArdaRHIShaderRef mDomainShader;
        /** Geometry shader used by the pipeline. */
        FArdaRHIShaderRef mGeometryShader;
        /** Pixel shader used by the pipeline. */
        FArdaRHIShaderRef mPixelShader;
        /** Stores the binding layouts. */
        eastl::vector<FArdaRHIBindingLayoutRef> mBindingLayouts;
        /** Stores the blend state. */
        FArdaRHIBlendState mBlendState;
        /** Stores the raster state. */
        FArdaRHIRasterState mRasterState;
        /** Stores the depth stencil state. */
        FArdaRHIDepthStencilState mDepthStencilState;
        /** Stores the color formats. */
        eastl::vector<EArdaRHIFormat> mColorFormats;
        /** Stores the depth format. */
        EArdaRHIFormat mDepthFormat = EArdaRHIFormat::Unknown;
        /** Stores the sample count. */
        uint32_t mSampleCount = 1;
        /**
         * Stable metadata key used by backend-native persistent caches.
         * Zero disables named lookup. It is intentionally ignored by equality
         * and semantic descriptor hashing.
         */
        uint64_t mPersistentCacheKey = 0;
        /** Stores the debug name. */
        eastl::string mDebugName;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIGraphicsPipelineDesc& O) const noexcept
        {
            return mTopology == O.mTopology && mPatchControlPoints == O.mPatchControlPoints &&
                mInputLayout == O.mInputLayout && mVertexShader == O.mVertexShader &&
                mHullShader == O.mHullShader && mDomainShader == O.mDomainShader &&
                mGeometryShader == O.mGeometryShader && mPixelShader == O.mPixelShader &&
                mBindingLayouts == O.mBindingLayouts && mBlendState == O.mBlendState &&
                mRasterState == O.mRasterState && mDepthStencilState == O.mDepthStencilState &&
                mColorFormats == O.mColorFormats && mDepthFormat == O.mDepthFormat &&
                mSampleCount == O.mSampleCount;
        }
    };

    /** Interface for graphics pipeline. */
    class IArdaRHIGraphicsPipeline : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIGraphicsPipelineDesc& GetDesc() const noexcept = 0;
    };

    /** Describes compute pipeline desc. */
    struct FArdaRHIComputePipelineDesc
    {
        /** Stores the compute shader. */
        FArdaRHIShaderRef mComputeShader;
        /** Stores the binding layouts. */
        eastl::vector<FArdaRHIBindingLayoutRef> mBindingLayouts;
        /**
         * Stable metadata key used by backend-native persistent caches.
         * Zero disables named lookup. It is intentionally ignored by equality
         * and semantic descriptor hashing.
         */
        uint64_t mPersistentCacheKey = 0;
        /** Stores the debug name. */
        eastl::string mDebugName;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIComputePipelineDesc& O) const noexcept
        {
            return mComputeShader == O.mComputeShader &&
                mBindingLayouts == O.mBindingLayouts;
        }
    };

    /** Interface for compute pipeline. */
    class IArdaRHIComputePipeline : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIComputePipelineDesc& GetDesc() const noexcept = 0;
    };

    /** Describes meshlet pipeline desc. */
    struct FArdaRHIMeshletPipelineDesc
    {
        /** Stores the topology. */
        EArdaRHIPrimitiveTopology mTopology = EArdaRHIPrimitiveTopology::TriangleList;
        /** Stores the amplification shader. */
        FArdaRHIShaderRef mAmplificationShader;
        /** Stores the mesh shader. */
        FArdaRHIShaderRef mMeshShader;
        /** Stores the pixel shader. */
        FArdaRHIShaderRef mPixelShader;
        /** Stores the binding layouts. */
        eastl::vector<FArdaRHIBindingLayoutRef> mBindingLayouts;
        /** Stores the blend state. */
        FArdaRHIBlendState mBlendState;
        /** Stores the raster state. */
        FArdaRHIRasterState mRasterState;
        /** Stores the depth stencil state. */
        FArdaRHIDepthStencilState mDepthStencilState;
        /** Stores the color formats. */
        eastl::vector<EArdaRHIFormat> mColorFormats;
        /** Stores the depth format. */
        EArdaRHIFormat mDepthFormat = EArdaRHIFormat::Unknown;
        /** Stores the sample count. */
        uint32_t mSampleCount = 1;
        /**
         * Stable metadata key used by backend-native persistent caches.
         * Zero disables named lookup. It is intentionally ignored by equality
         * and semantic descriptor hashing.
         */
        uint64_t mPersistentCacheKey = 0;
        /** Stores the debug name. */
        eastl::string mDebugName;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIMeshletPipelineDesc& O) const noexcept
        {
            return mTopology == O.mTopology &&
                mAmplificationShader == O.mAmplificationShader &&
                mMeshShader == O.mMeshShader && mPixelShader == O.mPixelShader &&
                mBindingLayouts == O.mBindingLayouts && mBlendState == O.mBlendState &&
                mRasterState == O.mRasterState && mDepthStencilState == O.mDepthStencilState &&
                mColorFormats == O.mColorFormats && mDepthFormat == O.mDepthFormat &&
                mSampleCount == O.mSampleCount;
        }
    };

    /** Interface for meshlet pipeline. */
    class IArdaRHIMeshletPipeline : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIMeshletPipelineDesc& GetDesc() const noexcept = 0;
    };

    /** Interface for raster state. */
    class IArdaRHIRasterState : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIRasterState& GetDesc() const noexcept = 0;
    };
    /** Interface for blend state. */
    class IArdaRHIBlendState : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIBlendState& GetDesc() const noexcept = 0;
    };
    /** Interface for depth stencil state. */
    class IArdaRHIDepthStencilState : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIDepthStencilState& GetDesc() const noexcept = 0;
    };

    /** Lifecycle state independently tracked for acceleration structures and micromaps. */
    enum class EArdaRHIAccelStructBuildState : uint8_t
    {
        Unbuilt,
        Built,
        Updated,
        Compacted
    };

    /** Describes opacity micromap usage count. */
    struct FArdaRHIOpacityMicromapUsageCount
    {
        /** Number of micromaps with this usage. */
        uint32_t mCount = 0;
        /** Subdivision level for this usage. */
        uint32_t mSubdivisionLevel = 0;
        /** Stores the format. */
        EArdaRHIOpacityMicromapFormat mFormat = EArdaRHIOpacityMicromapFormat::TwoState;
    };

    /** Describes opacity micromap desc. */
    struct FArdaRHIOpacityMicromapDesc
    {
        /** Stores the flags. */
        EArdaRHIOpacityMicromapBuildFlags mFlags = EArdaRHIOpacityMicromapBuildFlags::None;
        /** Stores the counts. */
        eastl::vector<FArdaRHIOpacityMicromapUsageCount> mCounts;
        /** Stores the input buffer. */
        FArdaRHIBufferRef mInputBuffer;
        /** Stores the input buffer offset. */
        uint64_t mInputBufferOffset = 0;
        /** Stores the per micromap desc buffer. */
        FArdaRHIBufferRef mPerMicromapDescBuffer;
        /** Stores the per micromap desc buffer offset. */
        uint64_t mPerMicromapDescBufferOffset = 0;
        /** Optional storage size for a destination created from a compacted-size query. */
        uint64_t mResultSizeOverride = 0;
        /** Stores the track liveness. */
        bool mbTrackLiveness = true;
        /** Must be true when disabling backend liveness tracking. */
        bool mbAllowUnsafeLivenessOptOut = false;
        /** Stores the debug name. */
        eastl::string mDebugName;
    };

    /** Interface for opacity micromap. */
    class IArdaRHIOpacityMicromap : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIOpacityMicromapDesc& GetDesc() const noexcept = 0;
        /**
         * Tests whether the compacted.
         * @return True when the condition is satisfied; otherwise false.
         */
        [[nodiscard]] virtual bool IsCompacted() const noexcept = 0;
        /**
         * Returns the device address.
         * @return The requested numeric value.
         */
        [[nodiscard]] virtual uint64_t GetDeviceAddress() const noexcept = 0;
        /** Native micromap handle identity used by conformance diagnostics. */
        [[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
        /** Last successfully submitted build lifecycle state. */
        [[nodiscard]] virtual EArdaRHIAccelStructBuildState
            GetBuildState() const noexcept
        {
            return EArdaRHIAccelStructBuildState::Unbuilt;
        }
    };

    /** Describes ray tracing geometry desc. */
    struct FArdaRHIRayTracingGeometryDesc
    {
        /** Stores the type. */
        EArdaRHIRayTracingGeometryType mType = EArdaRHIRayTracingGeometryType::Triangles;
        /** Stores the flags. */
        EArdaRHIRayTracingGeometryFlags mFlags = EArdaRHIRayTracingGeometryFlags::None;
        /** Stores the index buffer. */
        FArdaRHIBufferRef mIndexBuffer;
        /** Stores the vertex or aabbbuffer. */
        FArdaRHIBufferRef mVertexOrAABBBuffer;
        /** Stores the index format. */
        EArdaRHIFormat mIndexFormat = EArdaRHIFormat::Unknown;
        /** Stores the vertex format. */
        EArdaRHIFormat mVertexFormat = EArdaRHIFormat::Unknown;
        /** Byte offset into the index buffer. */
        uint64_t mIndexOffset = 0;
        /** Byte offset into the vertex or AABB buffer. */
        uint64_t mVertexOrAABBOffset = 0;
        /** Number of indices. */
        uint32_t mIndexCount = 0;
        /** Number of vertices or AABBs. */
        uint32_t mVertexOrAABBCount = 0;
        /** Vertex or AABB element stride in bytes. */
        uint32_t mStride = 0;
        /** Stores the opacity micromap. */
        FArdaRHIOpacityMicromapRef mOpacityMicromap;
        /** Stores the opacity micromap index buffer. */
        FArdaRHIBufferRef mOpacityMicromapIndexBuffer;
        /** Stores the opacity micromap index offset. */
        uint64_t mOpacityMicromapIndexOffset = 0;
        /** Stores the opacity micromap index format. */
        EArdaRHIFormat mOpacityMicromapIndexFormat = EArdaRHIFormat::Unknown;
        /** Stores the opacity micromap usage counts. */
        eastl::vector<FArdaRHIOpacityMicromapUsageCount> mOpacityMicromapUsageCounts;
    };

    /** Describes accel struct desc. */
    struct FArdaRHIAccelStructDesc
    {
        /** Stores the top level max instances. */
        size_t mTopLevelMaxInstances = 0;
        /** Stores the bottom level geometries. */
        eastl::vector<FArdaRHIRayTracingGeometryDesc> mBottomLevelGeometries;
        /** Stores the build flags. */
        EArdaRHIAccelStructBuildFlags mBuildFlags = EArdaRHIAccelStructBuildFlags::None;
        /** Stores the track liveness. */
        bool mbTrackLiveness = true;
        /** Must be true when disabling backend liveness tracking. */
        bool mbAllowUnsafeLivenessOptOut = false;
        /** Stores the top level. */
        bool mbTopLevel = false;
        /** Stores the virtual. */
        bool mbVirtual = false;
        /** Optional exact result size, used for a compacted destination. */
        uint64_t mResultSizeOverride = 0;
        /** Stores the debug name. */
        eastl::string mDebugName;
    };

    /** Native sizes required to build or update an acceleration structure. */
    struct FArdaRHIAccelStructMemoryRequirements
    {
        uint64_t mResultSize = 0;
        uint64_t mBuildScratchSize = 0;
        uint64_t mUpdateScratchSize = 0;
        uint64_t mResultAlignment = 0;
        uint64_t mScratchAlignment = 0;
    };

    /** Interface for accel struct. */
    class IArdaRHIAccelStruct : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIAccelStructDesc& GetDesc() const noexcept = 0;
        /**
         * Tests whether the compacted.
         * @return True when the condition is satisfied; otherwise false.
         */
        [[nodiscard]] virtual bool IsCompacted() const noexcept = 0;
        /**
         * Returns the device address.
         * @return The requested numeric value.
         */
        [[nodiscard]] virtual uint64_t GetDeviceAddress() const noexcept = 0;
        /** Returns the physical identity. */
        [[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
        /** Last successfully submitted build/compact lifecycle state. */
        [[nodiscard]] virtual EArdaRHIAccelStructBuildState
            GetBuildState() const noexcept
        {
            return EArdaRHIAccelStructBuildState::Unbuilt;
        }
    };

    /** Describes ray tracing pipeline shader desc. */
    struct FArdaRHIRayTracingPipelineShaderDesc
    {
        /** Stores the export name. */
        eastl::string mExportName;
        /** Stores the shader. */
        FArdaRHIShaderRef mShader;
        /** Stores the local binding layout. */
        FArdaRHIBindingLayoutRef mLocalBindingLayout;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIRayTracingPipelineShaderDesc& O) const noexcept
        {
            return mExportName == O.mExportName && mShader == O.mShader &&
                mLocalBindingLayout == O.mLocalBindingLayout;
        }
    };

    /** Describes ray tracing hit group desc. */
    struct FArdaRHIRayTracingHitGroupDesc
    {
        /** Stores the export name. */
        eastl::string mExportName;
        /** Closest-hit shader exported by the hit group. */
        FArdaRHIShaderRef mClosestHitShader;
        /** Any-hit shader exported by the hit group. */
        FArdaRHIShaderRef mAnyHitShader;
        /** Intersection shader exported by the hit group. */
        FArdaRHIShaderRef mIntersectionShader;
        /** Stores the local binding layout. */
        FArdaRHIBindingLayoutRef mLocalBindingLayout;
        /** Stores the procedural primitive. */
        bool mbProceduralPrimitive = false;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIRayTracingHitGroupDesc& O) const noexcept
        {
            return mExportName == O.mExportName &&
                mClosestHitShader == O.mClosestHitShader &&
                mAnyHitShader == O.mAnyHitShader &&
                mIntersectionShader == O.mIntersectionShader &&
                mLocalBindingLayout == O.mLocalBindingLayout &&
                mbProceduralPrimitive == O.mbProceduralPrimitive;
        }
    };

    /** Describes ray tracing pipeline desc. */
    struct FArdaRHIRayTracingPipelineDesc
    {
        /** Stores the shaders. */
        eastl::vector<FArdaRHIRayTracingPipelineShaderDesc> mShaders;
        /** Stores the hit groups. */
        eastl::vector<FArdaRHIRayTracingHitGroupDesc> mHitGroups;
        /** Stores the global binding layouts. */
        eastl::vector<FArdaRHIBindingLayoutRef> mGlobalBindingLayouts;
        /** Maximum ray payload size in bytes. */
        uint32_t mMaxPayloadSize = 0;
        /** Maximum intersection attribute size in bytes. */
        uint32_t mMaxAttributeSize = sizeof(float) * 2;
        /** Stores the max recursion depth. */
        uint32_t mMaxRecursionDepth = 1;
        /** Stores the allow opacity micromaps. */
        bool mbAllowOpacityMicromaps = false;
        /** Stores the debug name. */
        eastl::string mDebugName;
        /**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
        bool operator==(const FArdaRHIRayTracingPipelineDesc& O) const noexcept
        {
            return mShaders == O.mShaders && mHitGroups == O.mHitGroups &&
                mGlobalBindingLayouts == O.mGlobalBindingLayouts &&
                mMaxPayloadSize == O.mMaxPayloadSize &&
                mMaxAttributeSize == O.mMaxAttributeSize &&
                mMaxRecursionDepth == O.mMaxRecursionDepth &&
                mbAllowOpacityMicromaps == O.mbAllowOpacityMicromaps;
        }
    };

    /** Interface for ray tracing pipeline. */
    class IArdaRHIRayTracingPipeline : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIRayTracingPipelineDesc& GetDesc() const noexcept = 0;
    };

    /** Describes shader table desc. */
    struct FArdaRHIShaderTableDesc
    {
        /** Stores the cached. */
        bool mbCached = false;
        /** Stores the max entries. */
        uint32_t mMaxEntries = 0;
        /** Maximum bytes copied after the native shader identifier in a record. */
        uint32_t mMaxLocalArgumentBytes = 0;
        /** Table contents persist until explicitly replaced. */
        bool mbPersistent = false;
        /** Stores the debug name. */
        eastl::string mDebugName;
    };

    /** Category of a shader-binding-table record. */
    enum class EArdaRHIShaderTableRecordType : uint8_t
    {
        RayGeneration,
        Miss,
        HitGroup,
        Callable
    };

    /** Complete portable shader-binding-table record. */
    struct FArdaRHIShaderTableRecordDesc
    {
        EArdaRHIShaderTableRecordType mType =
            EArdaRHIShaderTableRecordType::RayGeneration;
        uint32_t mRecordIndex = 0;
        eastl::string mExportName;
        FArdaRHIBindingSetRef mBindings;
        eastl::vector<uint8_t> mLocalArguments;
        uint32_t mUserData = 0;
        FArdaRHIAccelStructRef mGeometry;
        uint32_t mGeometrySegment = 0;
    };

    /** Interface for shader table. */
    class IArdaRHIShaderTable : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHIShaderTableDesc& GetDesc() const noexcept = 0;
        /**
         * Returns the entry count.
         * @return The requested numeric value.
         */
        [[nodiscard]] virtual uint32_t GetEntryCount() const noexcept = 0;
    };

    /** Work-graph executable state object and backing-memory policy. */
    struct FArdaRHIWorkGraphPipelineDesc
    {
        eastl::string mProgramName;
        eastl::string mEntryPoint;
        eastl::vector<FArdaRHIShaderRef> mShaders;
        eastl::vector<FArdaRHIBindingLayoutRef> mGlobalBindingLayouts;
        uint32_t mMaxInputRecords = 1;
        eastl::string mDebugName;
    };

    class IArdaRHIWorkGraphPipeline : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIWorkGraphPipelineDesc&
            GetDesc() const noexcept = 0;
        [[nodiscard]] virtual uint64_t GetBackingMemorySize() const noexcept = 0;
    };

    /** One executable compute or mesh record in a shader bundle. */
    struct FArdaRHIShaderBundleRecord
    {
        FArdaRHIComputePipelineRef mComputePipeline;
        FArdaRHIMeshletPipelineRef mMeshPipeline;
        eastl::vector<FArdaRHIBindingSetRef> mBindings;
        eastl::vector<uint8_t> mLocalArguments;
        uint32_t mGroupsX = 1;
        uint32_t mGroupsY = 1;
        uint32_t mGroupsZ = 1;
    };

    struct FArdaRHIShaderBundleDesc
    {
        uint32_t mMaxRecords = 0;
        bool mbMeshRecords = false;
        bool mbPersistent = false;
        eastl::string mDebugName;
    };

    class IArdaRHIShaderBundle : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIShaderBundleDesc&
            GetDesc() const noexcept = 0;
        [[nodiscard]] virtual uint32_t GetRecordCount() const noexcept = 0;
    };

    /** Describes sampler feedback texture desc. */
    struct FArdaRHISamplerFeedbackTextureDesc
    {
        /** Stores the format. */
        EArdaRHISamplerFeedbackFormat mFormat = EArdaRHISamplerFeedbackFormat::MinMipOpaque;
        /** Feedback mip-region width. */
        uint32_t mMipRegionX = 0;
        /** Feedback mip-region height. */
        uint32_t mMipRegionY = 0;
        /** Feedback mip-region depth. */
        uint32_t mMipRegionZ = 0;
        /** Stores the initial state. */
        EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
        /** Stores the keep initial state. */
        bool mbKeepInitialState = false;
        /** Stores the debug name. */
        eastl::string mDebugName;
    };

    /** Interface for sampler feedback texture. */
    class IArdaRHISamplerFeedbackTexture : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the desc.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHISamplerFeedbackTextureDesc& GetDesc() const noexcept = 0;
        /**
         * Returns the paired texture.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHITextureRef& GetPairedTexture() const noexcept = 0;
        /** Native feedback-map identity used by state-conformance diagnostics. */
        [[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
    };

    /** Describes vertex buffer binding. */
    struct FArdaRHIVertexBufferBinding
    {
        /** Stores the buffer. */
        FArdaRHIBufferRef mBuffer;
        /** Stores the slot. */
        uint32_t mSlot = 0;
        /** Stores the offset. */
        uint64_t mOffset = 0;
    };

    /** Describes graphics state. */
    struct FArdaRHIGraphicsState
    {
        /** Stores the pipeline. */
        FArdaRHIGraphicsPipelineRef mPipeline;
        /** Stores the framebuffer. */
        FArdaRHIFramebufferRef mFramebuffer;
        /** Stores the bindings. */
        eastl::vector<FArdaRHIBindingSetRef> mBindings;
        /** Stores the vertex buffers. */
        eastl::vector<FArdaRHIVertexBufferBinding> mVertexBuffers;
        /** Stores the index buffer. */
        FArdaRHIBufferRef mIndexBuffer;
        /** Stores the index format. */
        EArdaRHIFormat mIndexFormat = EArdaRHIFormat::Unknown;
        /** Stores the index offset. */
        uint32_t mIndexOffset = 0;
        /** Stores the viewports. */
        eastl::vector<FArdaRHIViewport> mViewports;
        /** Stores the scissors. */
        eastl::vector<FArdaRHIRect> mScissors;
    };

    /** Describes compute state. */
    struct FArdaRHIComputeState
    {
        /** Stores the pipeline. */
        FArdaRHIComputePipelineRef mPipeline;
        /** Stores the bindings. */
        eastl::vector<FArdaRHIBindingSetRef> mBindings;
    };

    /** Describes meshlet state. */
    struct FArdaRHIMeshletState
    {
        /** Stores the pipeline. */
        FArdaRHIMeshletPipelineRef mPipeline;
        /** Stores the framebuffer. */
        FArdaRHIFramebufferRef mFramebuffer;
        /** Stores the bindings. */
        eastl::vector<FArdaRHIBindingSetRef> mBindings;
        /** Stores the viewports. */
        eastl::vector<FArdaRHIViewport> mViewports;
        /** Stores the scissors. */
        eastl::vector<FArdaRHIRect> mScissors;
    };

    /** Describes ray tracing state. */
    struct FArdaRHIRayTracingState
    {
        /** Stores the shader table. */
        FArdaRHIShaderTableRef mShaderTable;
        /** Stores the bindings. */
        eastl::vector<FArdaRHIBindingSetRef> mBindings;
    };

    /** Describes ray tracing instance desc. */
    struct FArdaRHIRayTracingInstanceDesc
    {
        /** Stores the transform. */
        float mTransform[12] = { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f };
        /** Application-defined instance identifier. */
        uint32_t mInstanceId = 0;
        /** Visibility mask for the instance. */
        uint32_t mInstanceMask = 0xff;
        /** Hit-group table contribution for the instance. */
        uint32_t mHitGroupContribution = 0;
        /** Backend ray-tracing instance flags. */
        uint32_t mFlags = 0;
        /** Stores the bottom level accel struct. */
        FArdaRHIAccelStructRef mBottomLevelAccelStruct;
    };

    /** Describes texture tile mapping. */
    struct FArdaRHITextureTileMapping
    {
        /** Stores the coordinates. */
        eastl::vector<FArdaRHITiledTextureCoordinate> mCoordinates;
        /** Stores the regions. */
        eastl::vector<FArdaRHITiledTextureRegion> mRegions;
        /** Stores the byte offsets. */
        eastl::vector<uint64_t> mByteOffsets;
        /** Stores the heap. */
        FArdaRHIHeapRef mHeap;
    };

    /** Describes texture tiling. */
    struct FArdaRHITextureTiling
    {
        /** Stores the tile count. */
        uint32_t mTileCount = 0;
        /** Stores the packed mips. */
        FArdaRHIPackedMipDesc mPackedMips;
        /** Stores the tile shape. */
        FArdaRHITileShape mTileShape;
        /** Stores the subresources. */
        eastl::vector<FArdaRHISubresourceTiling> mSubresources;
    };

    /** Generic contiguous tile mapping for sparse/reserved buffers. */
    struct FArdaRHIBufferTileMapping
    {
        uint64_t mBufferOffset = 0;
        uint64_t mByteSize = 0;
        uint64_t mHeapOffset = 0;
        FArdaRHIHeapRef mHeap;
        bool mbCommit = true;
    };

    /** Current local or non-local GPU memory budget telemetry. */
    struct FArdaRHIStreamingBudget
    {
        uint64_t mBudgetBytes = 0;
        uint64_t mCurrentUsageBytes = 0;
        uint64_t mAvailableForReservationBytes = 0;
        uint64_t mCurrentReservationBytes = 0;
        bool mbLocalMemory = true;
    };

    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIInputLayoutDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIGraphicsPipelineDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIComputePipelineDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIMeshletPipelineDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIRayTracingPipelineShaderDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIRayTracingHitGroupDesc& Value) noexcept;
    /**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
    [[nodiscard]] size_t HashValue(const FArdaRHIRayTracingPipelineDesc& Value) noexcept;
    /**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIInputLayoutDesc& Value);
    /**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIGraphicsPipelineDesc& Value);
    /**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIComputePipelineDesc& Value);
    /**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIMeshletPipelineDesc& Value);
    /**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIRayTracingPipelineDesc& Value);
}
