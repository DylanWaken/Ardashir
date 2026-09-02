/** @file RHI/ArdaRHIProvider.h
 * Backend-provider SPI consumed by ArdaBackend's concrete RHI implementation.
 */
#pragma once

#include "RHI/ArdaRHI.h"

#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

namespace arda::rhi::provider
{
    class IArdaProviderObject
    {
    public:
        virtual ~IArdaProviderObject() = default;
        [[nodiscard]] virtual const void* GetIdentity() const noexcept = 0;
        [[nodiscard]] virtual uint32_t GetDescriptorBaseIndex() const noexcept
        {
            return 0;
        }
        [[nodiscard]] virtual uint64_t
            GetWorkGraphBackingMemorySize() const noexcept
        {
            return 0;
        }
    };

    using FArdaProviderObjectRef = eastl::shared_ptr<IArdaProviderObject>;
    using FArdaProviderObjectResult = TArdaRHIResult<FArdaProviderObjectRef>;

    struct FArdaProviderLifetimeStats
    {
        size_t mResourceDescriptors = 0;
        size_t mSamplerDescriptors = 0;
        size_t mDescriptorSets = 0;
        size_t mPendingSubmissions = 0;
    };

    struct FArdaProviderBinding
    {
        FArdaRHIBindingItem mItem;
        FArdaProviderObjectRef mObject;
    };

    struct FArdaProviderTextureTileMapping
    {
        eastl::vector<FArdaRHITiledTextureCoordinate> mCoordinates;
        eastl::vector<FArdaRHITiledTextureRegion> mRegions;
        eastl::vector<uint64_t> mByteOffsets;
        FArdaProviderObjectRef mHeap;
    };

    struct FArdaProviderBufferTileMapping
    {
        uint64_t mBufferOffset = 0;
        uint64_t mByteSize = 0;
        uint64_t mHeapOffset = 0;
        FArdaProviderObjectRef mHeap;
        bool mbCommit = true;
    };

    struct FArdaProviderFramebufferTarget
    {
        FArdaRHIFramebufferTarget mTarget;
        FArdaProviderObjectRef mTexture;
    };

    struct FArdaProviderFramebufferCreateInfo
    {
        const FArdaRHIFramebufferDesc& mDesc;
        eastl::vector<FArdaProviderFramebufferTarget> mColors;
        FArdaProviderFramebufferTarget mDepth;
    };

    struct FArdaProviderGraphicsPipelineCreateInfo
    {
        const FArdaRHIGraphicsPipelineDesc& mDesc;
        const FArdaRHIInputLayoutDesc* mInputLayout = nullptr;
        FArdaProviderObjectRef mVertexShader;
        FArdaProviderObjectRef mHullShader;
        FArdaProviderObjectRef mDomainShader;
        FArdaProviderObjectRef mGeometryShader;
        FArdaProviderObjectRef mPixelShader;
        eastl::vector<FArdaProviderObjectRef> mBindingLayouts;
    };

    struct FArdaProviderComputePipelineCreateInfo
    {
        const FArdaRHIComputePipelineDesc& mDesc;
        FArdaProviderObjectRef mComputeShader;
        eastl::vector<FArdaProviderObjectRef> mBindingLayouts;
    };

    struct FArdaProviderMeshletPipelineCreateInfo
    {
        const FArdaRHIMeshletPipelineDesc& mDesc;
        FArdaProviderObjectRef mAmplificationShader;
        FArdaProviderObjectRef mMeshShader;
        FArdaProviderObjectRef mPixelShader;
        eastl::vector<FArdaProviderObjectRef> mBindingLayouts;
    };

    struct FArdaProviderWorkGraphPipelineCreateInfo
    {
        FArdaRHIWorkGraphPipelineDesc mDesc;
        eastl::vector<FArdaProviderObjectRef> mShaders;
        eastl::vector<FArdaProviderObjectRef> mBindingLayouts;
    };

    struct FArdaProviderRayTracingShader
    {
        eastl::string mExportName;
        eastl::string mEntryPoint;
        FArdaProviderObjectRef mShader;
        FArdaProviderObjectRef mLocalBindingLayout;
    };

    struct FArdaProviderRayTracingHitGroup
    {
        eastl::string mExportName;
        FArdaProviderRayTracingShader mClosestHit;
        FArdaProviderRayTracingShader mAnyHit;
        FArdaProviderRayTracingShader mIntersection;
        FArdaProviderObjectRef mLocalBindingLayout;
        bool mbProceduralPrimitive = false;
    };

    struct FArdaProviderRayTracingPipelineCreateInfo
    {
        const FArdaRHIRayTracingPipelineDesc& mDesc;
        eastl::vector<FArdaProviderRayTracingShader> mShaders;
        eastl::vector<FArdaProviderRayTracingHitGroup> mHitGroups;
        eastl::vector<FArdaProviderObjectRef> mGlobalBindingLayouts;
    };

    struct FArdaProviderVertexBufferBinding
    {
        FArdaProviderObjectRef mBuffer;
        uint32_t mSlot = 0;
        uint64_t mOffset = 0;
        uint32_t mStride = 0;
        uint64_t mSize = 0;
    };

    struct FArdaProviderGraphicsState
    {
        FArdaProviderObjectRef mPipeline;
        FArdaProviderObjectRef mFramebuffer;
        eastl::vector<FArdaProviderObjectRef> mBindings;
        eastl::vector<FArdaProviderVertexBufferBinding> mVertexBuffers;
        FArdaProviderObjectRef mIndexBuffer;
        EArdaRHIFormat mIndexFormat = EArdaRHIFormat::R32UInt;
        uint64_t mIndexOffset = 0;
        eastl::vector<FArdaRHIViewport> mViewports;
        eastl::vector<FArdaRHIRect> mScissors;
    };

    struct FArdaProviderComputeState
    {
        FArdaProviderObjectRef mPipeline;
        eastl::vector<FArdaProviderObjectRef> mBindings;
    };

    struct FArdaProviderMeshletState
    {
        FArdaProviderObjectRef mPipeline;
        FArdaProviderObjectRef mFramebuffer;
        eastl::vector<FArdaProviderObjectRef> mBindings;
        eastl::vector<FArdaRHIViewport> mViewports;
        eastl::vector<FArdaRHIRect> mScissors;
    };

    struct FArdaProviderRayTracingState
    {
        FArdaProviderObjectRef mShaderTable;
        eastl::vector<FArdaProviderObjectRef> mBindings;
    };

    /** Backend-resolved BLAS geometry. Facade references never cross this boundary. */
    struct FArdaProviderRayTracingGeometry
    {
        FArdaRHIRayTracingGeometryDesc mDesc;
        FArdaProviderObjectRef mIndexBuffer;
        FArdaProviderObjectRef mVertexOrAABBBuffer;
        FArdaProviderObjectRef mOpacityMicromap;
        FArdaProviderObjectRef mOpacityMicromapIndexBuffer;
    };

    /** Backend-resolved TLAS instance. */
    struct FArdaProviderRayTracingInstance
    {
        float mTransform[3][4]{};
        uint32_t mInstanceID = 0;
        uint32_t mInstanceMask = 0xff;
        uint32_t mInstanceContributionToHitGroupIndex = 0;
        uint32_t mFlags = 0;
        FArdaProviderObjectRef mBottomLevelAccelStruct;
    };

    class IArdaProviderCommandList
    {
    public:
        virtual ~IArdaProviderCommandList() = default;
        virtual FArdaRHIStatus Open() = 0;
        virtual FArdaRHIStatus Close() = 0;
        virtual FArdaRHIStatus Reset() = 0;
        virtual FArdaRHIStatus WriteBuffer(
            const FArdaProviderObjectRef& Buffer,
            const FArdaRHIBufferDesc& Desc,
            const void* Data,
            size_t Size,
            uint64_t Offset) = 0;
        virtual FArdaRHIStatus CopyBuffer(
            const FArdaProviderObjectRef& Destination,
            uint64_t DestinationOffset,
            const FArdaProviderObjectRef& Source,
            uint64_t SourceOffset,
            uint64_t Size) = 0;
        virtual FArdaRHIStatus CopyTexture(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHITextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHITextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice) = 0;
        virtual FArdaRHIStatus ResolveTexture(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHITextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHITextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice) = 0;
        virtual FArdaRHIStatus CopyTextureToStaging(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHIStagingTextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHITextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice) = 0;
        virtual FArdaRHIStatus CopyTextureFromStaging(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHITextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHIStagingTextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice) = 0;
        virtual FArdaRHIStatus ClearTexture(
            const FArdaProviderObjectRef& Texture,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& Range,
            const FArdaRHIColor& Color) = 0;
        virtual FArdaRHIStatus ClearDepthStencilTexture(
            const FArdaProviderObjectRef& Texture,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& Range,
            bool bClearDepth,
            float Depth,
            bool bClearStencil,
            uint8_t Stencil) = 0;
        virtual FArdaRHIStatus SetTextureState(
            const FArdaProviderObjectRef& Texture,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& Range,
            EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus SetBufferState(
            const FArdaProviderObjectRef& Buffer,
            const FArdaRHIBufferDesc& Desc,
            EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus TransitionTexture(
            const FArdaProviderObjectRef& Texture,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureTransitionDesc& Transition) = 0;
        virtual FArdaRHIStatus TransitionBuffer(
            const FArdaProviderObjectRef& Buffer,
            const FArdaRHIBufferDesc& Desc,
            const FArdaRHIBufferTransitionDesc& Transition) = 0;
        virtual void SetAutomaticBarriers(bool bEnabled) = 0;
        virtual FArdaRHIStatus BeginTrackingTextureState(
            const FArdaProviderObjectRef& Texture,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& Range,
            EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus BeginTrackingBufferState(
            const FArdaProviderObjectRef& Buffer,
            const FArdaRHIBufferDesc& Desc,
            EArdaRHIResourceState State) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHINativeResourceState>
            QueryTextureState(
                const FArdaProviderObjectRef& Texture,
                const FArdaRHITextureDesc& Desc,
                const FArdaRHITextureSubresourceRange& Range) const = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHINativeResourceState>
            QueryBufferState(
                const FArdaProviderObjectRef& Buffer,
                const FArdaRHIBufferDesc& Desc) const = 0;
        virtual FArdaRHIStatus SetUAVBarriersForTexture(
            const FArdaProviderObjectRef& Texture,
            bool bEnabled) = 0;
        virtual FArdaRHIStatus SetUAVBarriersForBuffer(
            const FArdaProviderObjectRef& Buffer,
            bool bEnabled) = 0;
        virtual void CommitBarriers() = 0;
        virtual FArdaRHIStatus AliasingBarrier(
            const FArdaProviderObjectRef& ResourceBefore,
            const FArdaProviderObjectRef& ResourceAfter) = 0;
        virtual FArdaRHIStatus SetGraphicsState(
            const FArdaProviderGraphicsState& State) = 0;
        virtual FArdaRHIStatus SetComputeState(
            const FArdaProviderComputeState& State) = 0;
        virtual FArdaRHIStatus SetMeshletState(
            const FArdaProviderMeshletState&)
        {
            return FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "Mesh shaders are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus SetRayTracingState(
            const FArdaProviderRayTracingState&)
        {
            return FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "Ray tracing is unsupported by this backend provider.");
        }
        virtual void SetPushConstants(const void* Data, size_t Size) = 0;
        virtual void Draw(const FArdaRHIDrawArguments& Arguments) = 0;
        virtual void DrawIndexed(const FArdaRHIDrawArguments& Arguments) = 0;
        virtual FArdaRHIStatus DrawIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset,
            uint32_t DrawCount,
            uint32_t Stride) = 0;
        virtual FArdaRHIStatus DrawIndexedIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset,
            uint32_t DrawCount,
            uint32_t Stride) = 0;
        virtual void Dispatch(uint32_t GroupsX, uint32_t GroupsY, uint32_t GroupsZ) = 0;
        virtual FArdaRHIStatus DispatchIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset) = 0;
        virtual FArdaRHIStatus DispatchMesh(
            uint32_t, uint32_t, uint32_t)
        {
            return FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "Mesh shaders are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus DispatchRays(
            uint32_t, uint32_t, uint32_t)
        {
            return FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "Ray tracing is unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus DispatchRaysIndirect(
            const FArdaProviderObjectRef&, uint64_t)
        {
            return FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "Indirect ray dispatch is unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus DispatchWorkGraph(
            const FArdaProviderObjectRef&, const void*, uint32_t, uint32_t,
            const eastl::vector<FArdaProviderObjectRef>&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Work graphs are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus ClearSamplerFeedbackTexture(
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Sampler feedback is unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus DecodeSamplerFeedbackTexture(
            const FArdaProviderObjectRef&, const FArdaRHITextureDesc&,
            const FArdaProviderObjectRef&, EArdaRHIFormat)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Sampler feedback is unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus SetSamplerFeedbackTextureState(
            const FArdaProviderObjectRef&, EArdaRHIResourceState)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Sampler feedback is unsupported by this backend provider.");
        }
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHINativeResourceState>
            QuerySamplerFeedbackTextureState(
                const FArdaProviderObjectRef&) const
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Sampler feedback is unsupported by this backend provider.")};
        }
        virtual FArdaRHIStatus SetAccelStructState(
            const FArdaProviderObjectRef&, EArdaRHIResourceState)
        {
            return FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "Acceleration structures are unsupported by this backend provider.");
        }
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHINativeResourceState>
            QueryAccelStructState(const FArdaProviderObjectRef&) const
        {
            return {{}, FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "Acceleration structures are unsupported by this backend provider.")};
        }
        virtual FArdaRHIStatus BuildBottomLevelAccelStruct(
            const FArdaProviderObjectRef&,
            const eastl::vector<FArdaProviderRayTracingGeometry>&,
            EArdaRHIAccelStructBuildFlags)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Acceleration structures are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus BuildTopLevelAccelStruct(
            const FArdaProviderObjectRef&,
            const eastl::vector<FArdaProviderRayTracingInstance>&,
            EArdaRHIAccelStructBuildFlags)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Acceleration structures are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus BuildTopLevelAccelStructFromBuffer(
            const FArdaProviderObjectRef&, const FArdaProviderObjectRef&,
            uint64_t, size_t, EArdaRHIAccelStructBuildFlags)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Acceleration structures are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus CompactAccelStruct(
            const FArdaProviderObjectRef&, const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Acceleration-structure compaction is unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus BuildOpacityMicromap(
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Opacity micromaps are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus CompactOpacityMicromap(
            const FArdaProviderObjectRef&, const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Opacity-micromap compaction is unsupported by this backend provider.");
        }
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHINativeResourceState>
            QueryOpacityMicromapState(const FArdaProviderObjectRef&) const
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Opacity micromaps are unsupported by this backend provider.")};
        }
        /** Records the start timestamp for a provider timer query. */
        virtual FArdaRHIStatus BeginTimerQuery(
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Timer queries are unsupported by this backend provider.");
        }
        /** Records the end timestamp and result resolve for a timer query. */
        virtual FArdaRHIStatus EndTimerQuery(
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Timer queries are unsupported by this backend provider.");
        }
        virtual void BeginMarker(const char* Name) = 0;
        virtual void EndMarker() = 0;
    };

    class IArdaRHIProviderDevice
    {
    public:
        virtual ~IArdaRHIProviderDevice() = default;
        [[nodiscard]] virtual const FArdaRHICapabilities& GetCapabilities() const noexcept = 0;
        [[nodiscard]] virtual EArdaRHINativeResourceType GetTextureImportType() const noexcept = 0;
        [[nodiscard]] virtual EArdaRHINativeResourceType GetBufferImportType() const noexcept = 0;

        [[nodiscard]] virtual FArdaProviderObjectResult CreateTexture(
            const FArdaRHITextureDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult
            CreateSamplerFeedbackTexture(
                const FArdaProviderObjectRef&,
                const FArdaRHITextureDesc&,
                const FArdaRHISamplerFeedbackTextureDesc&)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Sampler feedback is unsupported by this backend provider.")};
        }
        [[nodiscard]] virtual FArdaProviderObjectResult CreateBuffer(
            const FArdaRHIBufferDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult CreateHeap(
            const FArdaRHIHeapDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements>
            GetTextureMemoryRequirements(
                const FArdaProviderObjectRef& Texture,
                const FArdaRHITextureDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements>
            GetBufferMemoryRequirements(
                const FArdaProviderObjectRef& Buffer,
                const FArdaRHIBufferDesc& Desc) = 0;
        virtual FArdaRHIStatus BindTextureMemory(
            const FArdaProviderObjectRef& Texture,
            const FArdaRHITextureDesc& Desc,
            const FArdaProviderObjectRef& Heap,
            uint64_t Offset) = 0;
        virtual FArdaRHIStatus BindBufferMemory(
            const FArdaProviderObjectRef& Buffer,
            const FArdaRHIBufferDesc& Desc,
            const FArdaProviderObjectRef& Heap,
            uint64_t Offset) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureTiling>
            GetTextureTiling(const FArdaProviderObjectRef&)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Tiled textures are unsupported by this backend provider.")};
        }
        virtual FArdaRHIStatus UpdateTextureTileMappings(
            const FArdaProviderObjectRef&,
            const eastl::vector<FArdaProviderTextureTileMapping>&,
            EArdaRHIQueueType)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Tiled textures are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus UpdateBufferTileMappings(
            const FArdaProviderObjectRef&,
            const eastl::vector<FArdaProviderBufferTileMapping>&,
            EArdaRHIQueueType)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Sparse buffers are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus CommitReservedResource(
            const FArdaProviderObjectRef&, bool, uint64_t,
            EArdaRHIQueueType)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Reserved-resource commit is unsupported by this backend provider.");
        }
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIStreamingBudget>
            QueryStreamingBudget(bool) const
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Streaming budgets are unsupported by this backend provider.")};
        }
        virtual FArdaRHIStatus SetStreamingBudgetReservation(uint64_t, bool)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Streaming budget reservation is unsupported by this backend provider.");
        }
        [[nodiscard]] virtual FArdaProviderObjectResult CreateStagingTexture(
            const FArdaRHIStagingTextureDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIStagingTextureMapping>
            MapStagingTexture(
                const FArdaProviderObjectRef& Texture,
                const FArdaRHITextureSlice& Slice,
                EArdaRHICpuAccess Access) = 0;
        virtual FArdaRHIStatus UnmapStagingTexture(
            const FArdaProviderObjectRef& Texture) = 0;
        /** Maps a host-visible native buffer range. */
        [[nodiscard]] virtual TArdaRHIResult<void*> MapBuffer(
            const FArdaProviderObjectRef& Buffer,
            uint64_t Offset,
            size_t Size) = 0;
        /** Unmaps a native buffer previously returned by MapBuffer. */
        virtual void UnmapBuffer(const FArdaProviderObjectRef& Buffer) noexcept = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult ImportTexture(
            const FArdaRHINativeTextureImportDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult ImportBuffer(
            const FArdaRHINativeBufferImportDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult CreateSampler(
            const FArdaRHISamplerDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult CreateShader(
            const FArdaRHIShaderDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult CreateBindingLayout(
            const FArdaRHIBindingLayoutDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult CreateBindlessLayout(
            const FArdaRHIBindlessLayoutDesc&,
            const FArdaRHIBindingLayoutDesc& NativeDesc)
        {
            return CreateBindingLayout(NativeDesc);
        }
        [[nodiscard]] virtual FArdaProviderObjectResult CreateBindingSet(
            const FArdaRHIBindingSetDesc& Desc,
            const FArdaProviderObjectRef& Layout,
            const eastl::vector<FArdaProviderBinding>& Bindings) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult CreateFramebuffer(
            const FArdaProviderFramebufferCreateInfo& Info) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult CreateGraphicsPipeline(
            const FArdaProviderGraphicsPipelineCreateInfo& Info) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult CreateComputePipeline(
            const FArdaProviderComputePipelineCreateInfo& Info) = 0;
        [[nodiscard]] virtual FArdaProviderObjectResult CreateMeshletPipeline(
            const FArdaProviderMeshletPipelineCreateInfo&)
        {
            return {
                {},
                FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "Mesh shaders are unsupported by this backend provider.")
            };
        }
        [[nodiscard]] virtual FArdaProviderObjectResult
            CreateRayTracingPipeline(
                const FArdaProviderRayTracingPipelineCreateInfo&)
        {
            return {
                {},
                FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "Ray tracing is unsupported by this backend provider.")
            };
        }
        [[nodiscard]] virtual FArdaProviderObjectResult
            CreateWorkGraphPipeline(
                const FArdaProviderWorkGraphPipelineCreateInfo&)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Work graphs are unsupported by this backend provider.")};
        }
        [[nodiscard]] virtual FArdaProviderObjectResult CreateShaderTable(
            const FArdaProviderObjectRef&,
            const FArdaRHIShaderTableDesc&)
        {
            return {
                {},
                FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "Shader tables are unsupported by this backend provider.")
            };
        }
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements>
            GetAccelStructBuildMemoryRequirements(
                const FArdaRHIAccelStructDesc&,
                const eastl::vector<FArdaProviderRayTracingGeometry>&)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Acceleration structures are unsupported by this backend provider.")};
        }
        [[nodiscard]] virtual FArdaProviderObjectResult CreateAccelStruct(
            const FArdaRHIAccelStructDesc&,
            const FArdaRHIAccelStructMemoryRequirements&)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Acceleration structures are unsupported by this backend provider.")};
        }
        [[nodiscard]] virtual TArdaRHIResult<uint64_t>
            GetAccelStructCompactedSize(const FArdaProviderObjectRef&)
        {
            return {0, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Acceleration-structure compaction is unsupported by this backend provider.")};
        }
        [[nodiscard]] virtual uint64_t GetAccelStructDeviceAddress(
            const FArdaProviderObjectRef&) const noexcept
        {
            return 0;
        }
        [[nodiscard]] virtual FArdaProviderObjectResult CreateOpacityMicromap(
            const FArdaRHIOpacityMicromapDesc&,
            const FArdaProviderObjectRef&,
            const FArdaProviderObjectRef&)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Opacity micromaps are unsupported by this backend provider.")};
        }
        [[nodiscard]] virtual TArdaRHIResult<uint64_t>
            GetOpacityMicromapCompactedSize(const FArdaProviderObjectRef&)
        {
            return {0, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Opacity-micromap compaction is unsupported by this backend provider.")};
        }
        [[nodiscard]] virtual uint64_t GetOpacityMicromapDeviceAddress(
            const FArdaProviderObjectRef&) const noexcept
        {
            return 0;
        }
        virtual FArdaRHIStatus SetShaderTableRecord(
            const FArdaProviderObjectRef&,
            const FArdaRHIShaderTableRecordDesc&,
            const FArdaProviderObjectRef&,
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Complete shader-table records are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus CommitShaderTable(
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Explicit shader-table commits are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus SetShaderTableRayGeneration(
            const FArdaProviderObjectRef&,
            const char*,
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "Shader tables are unsupported by this backend provider.");
        }
        virtual FArdaRHIStatus AddShaderTableEntry(
            const FArdaProviderObjectRef&,
            const char*,
            const FArdaProviderObjectRef&,
            uint32_t)
        {
            return FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "Shader tables are unsupported by this backend provider.");
        }
        /** Creates a native queue-completion event query. */
        [[nodiscard]] virtual FArdaProviderObjectResult CreateEventQuery()
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Event queries are unsupported by this backend provider.")};
        }
        /** Creates a native timestamp query pair. */
        [[nodiscard]] virtual FArdaProviderObjectResult CreateTimerQuery()
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Timer queries are unsupported by this backend provider.")};
        }
        /** Creates a native GPU queue fence. */
        [[nodiscard]] virtual FArdaProviderObjectResult CreateGpuFence()
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "GPU fences are unsupported by this backend provider.")};
        }
        /** Inserts an event marker after all prior work on the queue. */
        virtual FArdaRHIStatus SignalEventQuery(
            const FArdaProviderObjectRef&, EArdaRHIQueueType)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Event queries are unsupported by this backend provider.");
        }
        /** Tests whether the native event marker has completed. */
        [[nodiscard]] virtual TArdaRHIResult<bool> PollEventQuery(
            const FArdaProviderObjectRef&)
        {
            return {false, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Event queries are unsupported by this backend provider.")};
        }
        /** Waits for the native event marker without idling unrelated queues. */
        virtual FArdaRHIStatus WaitEventQuery(
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Event queries are unsupported by this backend provider.");
        }
        /** Rearms a completed native event query. */
        virtual FArdaRHIStatus ResetEventQuery(
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Event queries are unsupported by this backend provider.");
        }
        /** Tests whether both native timer timestamps are available. */
        [[nodiscard]] virtual TArdaRHIResult<bool> PollTimerQuery(
            const FArdaProviderObjectRef&)
        {
            return {false, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Timer queries are unsupported by this backend provider.")};
        }
        /** Returns the elapsed native timestamp interval in seconds. */
        [[nodiscard]] virtual TArdaRHIResult<float> GetTimerQuerySeconds(
            const FArdaProviderObjectRef&)
        {
            return {0.f, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Timer queries are unsupported by this backend provider.")};
        }
        /** Rearms a completed native timer query. */
        virtual FArdaRHIStatus ResetTimerQuery(
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "Timer queries are unsupported by this backend provider.");
        }
        /** Inserts a native fence signal after all prior work on the queue. */
        virtual FArdaRHIStatus SignalGpuFence(
            const FArdaProviderObjectRef&, EArdaRHIQueueType)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "GPU fences are unsupported by this backend provider.");
        }
        /** Tests whether the native GPU fence has completed. */
        [[nodiscard]] virtual TArdaRHIResult<bool> PollGpuFence(
            const FArdaProviderObjectRef&)
        {
            return {false, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "GPU fences are unsupported by this backend provider.")};
        }
        /** Waits for the native GPU fence without idling unrelated queues. */
        virtual FArdaRHIStatus WaitGpuFence(
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "GPU fences are unsupported by this backend provider.");
        }
        /** Rearms a completed native GPU fence. */
        virtual FArdaRHIStatus ResetGpuFence(
            const FArdaProviderObjectRef&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "GPU fences are unsupported by this backend provider.");
        }
        [[nodiscard]] virtual TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>>
            CreateCommandList(EArdaRHIQueueType Queue, bool bImmediate) = 0;
        [[nodiscard]] virtual TArdaRHIResult<uint64_t> ExecuteCommandList(
            IArdaProviderCommandList& CommandList,
            EArdaRHIQueueType Queue) = 0;
        /** Adds a GPU-side dependency on a previously signaled submission. */
        virtual FArdaRHIStatus QueueWait(
            EArdaRHIQueueType WaitQueue,
            EArdaRHIQueueType ExecutionQueue,
            uint64_t Submission)
        {
            (void)WaitQueue;
            (void)ExecutionQueue;
            return WaitForSubmission(Submission);
        }
        /** Waits only for the requested submission when the API supports it. */
        virtual FArdaRHIStatus WaitForSubmission(uint64_t Submission)
        {
            (void)Submission;
            return WaitForIdle();
        }
        virtual FArdaRHIStatus WaitForIdle() = 0;
        virtual void RunGarbageCollection() = 0;
        [[nodiscard]] virtual FArdaProviderLifetimeStats
            GetLifetimeStats() const noexcept { return {}; }
        virtual void FlushPipelineCache() noexcept = 0;
    };

}
