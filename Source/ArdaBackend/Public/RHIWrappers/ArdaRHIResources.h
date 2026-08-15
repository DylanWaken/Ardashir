#pragma once

#include "ArdaRHIRef.h"
#include "ArdaRHIResource.h"
#include "ArdaRHITypes.h"

#include <EASTL/vector.h>

namespace arda::rhi
{
    class IArdaRHITexture : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHITextureDesc& GetDesc() const noexcept = 0;
        [[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
    };

    /** Mutable logical indirection to a texture; the referenced texture is retained. */
    class IArdaRHITextureReference : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHITextureRef& GetTexture() const noexcept = 0;
    };

    class IArdaRHIBuffer : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIBufferDesc& GetDesc() const noexcept = 0;
        [[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
    };

    struct FArdaRHIUniformBufferDesc
    {
        size_t mByteSize = 0;
        uint32_t mMaxVersions = 1;
        eastl::string mDebugName;
    };

    /** Constant-buffer resource with a stable logical identity. */
    class IArdaRHIUniformBuffer : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIUniformBufferDesc& GetDesc() const noexcept = 0;
        [[nodiscard]] virtual const FArdaRHIBufferRef& GetBuffer() const noexcept = 0;
    };

    struct FArdaRHIHeapDesc
    {
        uint64_t mCapacity = 0;
        EArdaRHIHeapType mType = EArdaRHIHeapType::DeviceLocal;
        eastl::string mDebugName;
    };

    class IArdaRHIHeap : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIHeapDesc& GetDesc() const noexcept = 0;
    };

    struct FArdaRHIStagingTextureDesc
    {
        FArdaRHITextureDesc mTexture;
        EArdaRHICpuAccess mCpuAccess = EArdaRHICpuAccess::Read;
        eastl::string mDebugName;
    };

    class IArdaRHIStagingTexture : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIStagingTextureDesc& GetDesc() const noexcept = 0;
    };

    class IArdaRHIEventQuery : public virtual IArdaRHIResource {};
    class IArdaRHITimerQuery : public virtual IArdaRHIResource {};
    /** Queue fence facade backed by an event query; one signal is tracked at a time. */
    class IArdaRHIGpuFence : public virtual IArdaRHIResource {};

    class IArdaRHIShaderResourceView : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual IArdaRHIResource* GetResource() const noexcept = 0;
        [[nodiscard]] virtual const FArdaRHIViewDesc& GetDesc() const noexcept = 0;
    };

    class IArdaRHIUnorderedAccessView : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual IArdaRHIResource* GetResource() const noexcept = 0;
        [[nodiscard]] virtual const FArdaRHIViewDesc& GetDesc() const noexcept = 0;
    };

    class IArdaRHISampler : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHISamplerDesc& GetDesc() const noexcept = 0;
    };

    class IArdaRHIShader : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual EArdaRHIShaderStage GetStage() const noexcept = 0;
    };

    class IArdaRHIShaderLibrary : public virtual IArdaRHIResource {};

    /** Complete deterministic key used to cache input layouts. */
    struct FArdaRHIInputLayoutDesc
    {
        eastl::vector<FArdaRHIVertexAttributeDesc> mAttributes;
        FArdaRHIShaderRef mVertexShader;
        bool operator==(const FArdaRHIInputLayoutDesc& O) const noexcept
        {
            return mAttributes == O.mAttributes && mVertexShader == O.mVertexShader;
        }
    };

    class IArdaRHIInputLayout : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIInputLayoutDesc& GetDesc() const noexcept = 0;
    };
    class IArdaRHIBindingLayout : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIBindingLayoutDesc& GetDesc() const noexcept = 0;
    };

    struct FArdaRHIBindingItem
    {
        uint32_t mSlot = 0;
        uint32_t mArrayElement = 0;
        EArdaRHIBindingType mType = EArdaRHIBindingType::TextureSRV;
        TArdaRHIRef<IArdaRHIResource> mResource;
        FArdaRHIViewDesc mView;
    };

    struct FArdaRHIBindingSetDesc
    {
        FArdaRHIBindingLayoutRef mLayout;
        eastl::vector<FArdaRHIBindingItem> mItems;
        eastl::string mDebugName;
    };

    class IArdaRHIBindingSet : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIBindingSetDesc& GetDesc() const noexcept = 0;
    };

    struct FArdaRHIBindlessLayoutDesc
    {
        EArdaRHIShaderStage mVisibility = EArdaRHIShaderStage::None;
        uint32_t mFirstSlot = 0;
        uint32_t mMaxCapacity = 0;
        EArdaRHIBindlessLayoutType mLayoutType = EArdaRHIBindlessLayoutType::Immutable;
        eastl::vector<FArdaRHIBindingLayoutItem> mRegisterSpaces;
        eastl::string mDebugName;
        /** Required opt-in because descriptor-table writes do not retain resources. */
        bool mbAllowUnsafeDescriptorTableLifetime = false;
    };

    /**
     * Mutable descriptor table. Written resources are deliberately not retained;
     * the caller must keep them alive through all GPU uses of the table.
     */
    class IArdaRHIDescriptorTable : public virtual IArdaRHIBindingSet
    {
    public:
        [[nodiscard]] virtual uint32_t GetCapacity() const noexcept = 0;
        [[nodiscard]] virtual uint32_t GetFirstDescriptorIndexInHeap() const noexcept = 0;
    };

    struct FArdaRHIFramebufferTarget
    {
        FArdaRHITextureRef mTexture;
        FArdaRHIFramebufferAttachment mAttachment;
    };

    struct FArdaRHIFramebufferDesc
    {
        eastl::vector<FArdaRHIFramebufferTarget> mColorAttachments;
        FArdaRHIFramebufferTarget mDepthAttachment;
        eastl::string mDebugName;
    };

    class IArdaRHIFramebuffer : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIFramebufferDesc& GetDesc() const noexcept = 0;
    };

    struct FArdaRHIGraphicsPipelineDesc
    {
        EArdaRHIPrimitiveTopology mTopology = EArdaRHIPrimitiveTopology::TriangleList;
        uint32_t mPatchControlPoints = 0;
        FArdaRHIInputLayoutRef mInputLayout;
        FArdaRHIShaderRef mVertexShader, mHullShader, mDomainShader, mGeometryShader, mPixelShader;
        eastl::vector<FArdaRHIBindingLayoutRef> mBindingLayouts;
        FArdaRHIBlendState mBlendState;
        FArdaRHIRasterState mRasterState;
        FArdaRHIDepthStencilState mDepthStencilState;
        eastl::vector<EArdaRHIFormat> mColorFormats;
        EArdaRHIFormat mDepthFormat = EArdaRHIFormat::Unknown;
        uint32_t mSampleCount = 1;
        eastl::string mDebugName;
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

    class IArdaRHIGraphicsPipeline : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIGraphicsPipelineDesc& GetDesc() const noexcept = 0;
    };

    struct FArdaRHIComputePipelineDesc
    {
        FArdaRHIShaderRef mComputeShader;
        eastl::vector<FArdaRHIBindingLayoutRef> mBindingLayouts;
        eastl::string mDebugName;
        bool operator==(const FArdaRHIComputePipelineDesc& O) const noexcept
        {
            return mComputeShader == O.mComputeShader &&
                mBindingLayouts == O.mBindingLayouts;
        }
    };

    class IArdaRHIComputePipeline : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIComputePipelineDesc& GetDesc() const noexcept = 0;
    };

    struct FArdaRHIMeshletPipelineDesc
    {
        EArdaRHIPrimitiveTopology mTopology = EArdaRHIPrimitiveTopology::TriangleList;
        FArdaRHIShaderRef mAmplificationShader;
        FArdaRHIShaderRef mMeshShader;
        FArdaRHIShaderRef mPixelShader;
        eastl::vector<FArdaRHIBindingLayoutRef> mBindingLayouts;
        FArdaRHIBlendState mBlendState;
        FArdaRHIRasterState mRasterState;
        FArdaRHIDepthStencilState mDepthStencilState;
        eastl::vector<EArdaRHIFormat> mColorFormats;
        EArdaRHIFormat mDepthFormat = EArdaRHIFormat::Unknown;
        uint32_t mSampleCount = 1;
        eastl::string mDebugName;
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

    class IArdaRHIMeshletPipeline : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIMeshletPipelineDesc& GetDesc() const noexcept = 0;
    };

    class IArdaRHIRasterState : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIRasterState& GetDesc() const noexcept = 0;
    };
    class IArdaRHIBlendState : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIBlendState& GetDesc() const noexcept = 0;
    };
    class IArdaRHIDepthStencilState : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIDepthStencilState& GetDesc() const noexcept = 0;
    };

    struct FArdaRHIOpacityMicromapUsageCount
    {
        uint32_t mCount = 0, mSubdivisionLevel = 0;
        EArdaRHIOpacityMicromapFormat mFormat = EArdaRHIOpacityMicromapFormat::TwoState;
    };

    struct FArdaRHIOpacityMicromapDesc
    {
        EArdaRHIOpacityMicromapBuildFlags mFlags = EArdaRHIOpacityMicromapBuildFlags::None;
        eastl::vector<FArdaRHIOpacityMicromapUsageCount> mCounts;
        FArdaRHIBufferRef mInputBuffer;
        uint64_t mInputBufferOffset = 0;
        FArdaRHIBufferRef mPerMicromapDescBuffer;
        uint64_t mPerMicromapDescBufferOffset = 0;
        bool mbTrackLiveness = true;
        /** Must be true when disabling backend liveness tracking. */
        bool mbAllowUnsafeLivenessOptOut = false;
        eastl::string mDebugName;
    };

    class IArdaRHIOpacityMicromap : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIOpacityMicromapDesc& GetDesc() const noexcept = 0;
        [[nodiscard]] virtual bool IsCompacted() const noexcept = 0;
        [[nodiscard]] virtual uint64_t GetDeviceAddress() const noexcept = 0;
    };

    struct FArdaRHIRayTracingGeometryDesc
    {
        EArdaRHIRayTracingGeometryType mType = EArdaRHIRayTracingGeometryType::Triangles;
        EArdaRHIRayTracingGeometryFlags mFlags = EArdaRHIRayTracingGeometryFlags::None;
        FArdaRHIBufferRef mIndexBuffer;
        FArdaRHIBufferRef mVertexOrAABBBuffer;
        EArdaRHIFormat mIndexFormat = EArdaRHIFormat::Unknown;
        EArdaRHIFormat mVertexFormat = EArdaRHIFormat::Unknown;
        uint64_t mIndexOffset = 0, mVertexOrAABBOffset = 0;
        uint32_t mIndexCount = 0, mVertexOrAABBCount = 0, mStride = 0;
        FArdaRHIOpacityMicromapRef mOpacityMicromap;
        FArdaRHIBufferRef mOpacityMicromapIndexBuffer;
        uint64_t mOpacityMicromapIndexOffset = 0;
        EArdaRHIFormat mOpacityMicromapIndexFormat = EArdaRHIFormat::Unknown;
        eastl::vector<FArdaRHIOpacityMicromapUsageCount> mOpacityMicromapUsageCounts;
    };

    struct FArdaRHIAccelStructDesc
    {
        size_t mTopLevelMaxInstances = 0;
        eastl::vector<FArdaRHIRayTracingGeometryDesc> mBottomLevelGeometries;
        EArdaRHIAccelStructBuildFlags mBuildFlags = EArdaRHIAccelStructBuildFlags::None;
        bool mbTrackLiveness = true;
        /** Must be true when disabling backend liveness tracking. */
        bool mbAllowUnsafeLivenessOptOut = false;
        bool mbTopLevel = false;
        bool mbVirtual = false;
        eastl::string mDebugName;
    };

    class IArdaRHIAccelStruct : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIAccelStructDesc& GetDesc() const noexcept = 0;
        [[nodiscard]] virtual bool IsCompacted() const noexcept = 0;
        [[nodiscard]] virtual uint64_t GetDeviceAddress() const noexcept = 0;
        [[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
    };

    struct FArdaRHIRayTracingPipelineShaderDesc
    {
        eastl::string mExportName;
        FArdaRHIShaderRef mShader;
        FArdaRHIBindingLayoutRef mLocalBindingLayout;
        bool operator==(const FArdaRHIRayTracingPipelineShaderDesc& O) const noexcept
        {
            return mExportName == O.mExportName && mShader == O.mShader &&
                mLocalBindingLayout == O.mLocalBindingLayout;
        }
    };

    struct FArdaRHIRayTracingHitGroupDesc
    {
        eastl::string mExportName;
        FArdaRHIShaderRef mClosestHitShader, mAnyHitShader, mIntersectionShader;
        FArdaRHIBindingLayoutRef mLocalBindingLayout;
        bool mbProceduralPrimitive = false;
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

    struct FArdaRHIRayTracingPipelineDesc
    {
        eastl::vector<FArdaRHIRayTracingPipelineShaderDesc> mShaders;
        eastl::vector<FArdaRHIRayTracingHitGroupDesc> mHitGroups;
        eastl::vector<FArdaRHIBindingLayoutRef> mGlobalBindingLayouts;
        uint32_t mMaxPayloadSize = 0, mMaxAttributeSize = sizeof(float) * 2;
        uint32_t mMaxRecursionDepth = 1;
        bool mbAllowOpacityMicromaps = false;
        eastl::string mDebugName;
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

    class IArdaRHIRayTracingPipeline : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIRayTracingPipelineDesc& GetDesc() const noexcept = 0;
    };

    struct FArdaRHIShaderTableDesc
    {
        bool mbCached = false;
        uint32_t mMaxEntries = 0;
        eastl::string mDebugName;
    };

    class IArdaRHIShaderTable : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHIShaderTableDesc& GetDesc() const noexcept = 0;
        [[nodiscard]] virtual uint32_t GetEntryCount() const noexcept = 0;
    };

    struct FArdaRHISamplerFeedbackTextureDesc
    {
        EArdaRHISamplerFeedbackFormat mFormat = EArdaRHISamplerFeedbackFormat::MinMipOpaque;
        uint32_t mMipRegionX = 0, mMipRegionY = 0, mMipRegionZ = 0;
        EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
        bool mbKeepInitialState = false;
        eastl::string mDebugName;
    };

    class IArdaRHISamplerFeedbackTexture : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHISamplerFeedbackTextureDesc& GetDesc() const noexcept = 0;
        [[nodiscard]] virtual const FArdaRHITextureRef& GetPairedTexture() const noexcept = 0;
    };

    struct FArdaRHIVertexBufferBinding
    {
        FArdaRHIBufferRef mBuffer;
        uint32_t mSlot = 0;
        uint64_t mOffset = 0;
    };

    struct FArdaRHIGraphicsState
    {
        FArdaRHIGraphicsPipelineRef mPipeline;
        FArdaRHIFramebufferRef mFramebuffer;
        eastl::vector<FArdaRHIBindingSetRef> mBindings;
        eastl::vector<FArdaRHIVertexBufferBinding> mVertexBuffers;
        FArdaRHIBufferRef mIndexBuffer;
        EArdaRHIFormat mIndexFormat = EArdaRHIFormat::Unknown;
        uint32_t mIndexOffset = 0;
        eastl::vector<FArdaRHIViewport> mViewports;
        eastl::vector<FArdaRHIRect> mScissors;
    };

    struct FArdaRHIComputeState
    {
        FArdaRHIComputePipelineRef mPipeline;
        eastl::vector<FArdaRHIBindingSetRef> mBindings;
    };

    struct FArdaRHIMeshletState
    {
        FArdaRHIMeshletPipelineRef mPipeline;
        FArdaRHIFramebufferRef mFramebuffer;
        eastl::vector<FArdaRHIBindingSetRef> mBindings;
        eastl::vector<FArdaRHIViewport> mViewports;
        eastl::vector<FArdaRHIRect> mScissors;
    };

    struct FArdaRHIRayTracingState
    {
        FArdaRHIShaderTableRef mShaderTable;
        eastl::vector<FArdaRHIBindingSetRef> mBindings;
    };

    struct FArdaRHIRayTracingInstanceDesc
    {
        float mTransform[12] = { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f };
        uint32_t mInstanceId = 0, mInstanceMask = 0xff;
        uint32_t mHitGroupContribution = 0, mFlags = 0;
        FArdaRHIAccelStructRef mBottomLevelAccelStruct;
    };

    struct FArdaRHITextureTileMapping
    {
        eastl::vector<FArdaRHITiledTextureCoordinate> mCoordinates;
        eastl::vector<FArdaRHITiledTextureRegion> mRegions;
        eastl::vector<uint64_t> mByteOffsets;
        FArdaRHIHeapRef mHeap;
    };

    struct FArdaRHITextureTiling
    {
        uint32_t mTileCount = 0;
        FArdaRHIPackedMipDesc mPackedMips;
        FArdaRHITileShape mTileShape;
        eastl::vector<FArdaRHISubresourceTiling> mSubresources;
    };

    [[nodiscard]] size_t HashValue(const FArdaRHIInputLayoutDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIGraphicsPipelineDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIComputePipelineDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIMeshletPipelineDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIRayTracingPipelineShaderDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIRayTracingHitGroupDesc& Value) noexcept;
    [[nodiscard]] size_t HashValue(const FArdaRHIRayTracingPipelineDesc& Value) noexcept;
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIInputLayoutDesc& Value);
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIGraphicsPipelineDesc& Value);
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIComputePipelineDesc& Value);
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIMeshletPipelineDesc& Value);
    [[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIRayTracingPipelineDesc& Value);
}
