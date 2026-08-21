/** @file ArdaNativeRHI.h
 * Shared implementation boundary for the native Vulkan and D3D12 modules.
 */
#pragma once

#include "ArdaBackend.h"

#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

namespace arda::rhi::native
{
    class IArdaNativeObject
    {
    public:
        virtual ~IArdaNativeObject() = default;
        [[nodiscard]] virtual const void* GetIdentity() const noexcept = 0;
    };

    using FArdaNativeObjectRef = eastl::shared_ptr<IArdaNativeObject>;
    using FArdaNativeObjectResult = TArdaRHIResult<FArdaNativeObjectRef>;

    struct FArdaNativeLifetimeStats
    {
        size_t mResourceDescriptors = 0;
        size_t mSamplerDescriptors = 0;
        size_t mDescriptorSets = 0;
        size_t mPendingSubmissions = 0;
    };

    struct FArdaNativeBinding
    {
        FArdaRHIBindingItem mItem;
        FArdaNativeObjectRef mObject;
    };

    struct FArdaNativeFramebufferTarget
    {
        FArdaRHIFramebufferTarget mTarget;
        FArdaNativeObjectRef mTexture;
    };

    struct FArdaNativeFramebufferCreateInfo
    {
        const FArdaRHIFramebufferDesc& mDesc;
        eastl::vector<FArdaNativeFramebufferTarget> mColors;
        FArdaNativeFramebufferTarget mDepth;
    };

    struct FArdaNativeGraphicsPipelineCreateInfo
    {
        const FArdaRHIGraphicsPipelineDesc& mDesc;
        const FArdaRHIInputLayoutDesc* mInputLayout = nullptr;
        FArdaNativeObjectRef mVertexShader;
        FArdaNativeObjectRef mHullShader;
        FArdaNativeObjectRef mDomainShader;
        FArdaNativeObjectRef mGeometryShader;
        FArdaNativeObjectRef mPixelShader;
        eastl::vector<FArdaNativeObjectRef> mBindingLayouts;
    };

    struct FArdaNativeComputePipelineCreateInfo
    {
        const FArdaRHIComputePipelineDesc& mDesc;
        FArdaNativeObjectRef mComputeShader;
        eastl::vector<FArdaNativeObjectRef> mBindingLayouts;
    };

    struct FArdaNativeVertexBufferBinding
    {
        FArdaNativeObjectRef mBuffer;
        uint32_t mSlot = 0;
        uint64_t mOffset = 0;
        uint32_t mStride = 0;
        uint64_t mSize = 0;
    };

    struct FArdaNativeGraphicsState
    {
        FArdaNativeObjectRef mPipeline;
        FArdaNativeObjectRef mFramebuffer;
        eastl::vector<FArdaNativeObjectRef> mBindings;
        eastl::vector<FArdaNativeVertexBufferBinding> mVertexBuffers;
        FArdaNativeObjectRef mIndexBuffer;
        EArdaRHIFormat mIndexFormat = EArdaRHIFormat::R32UInt;
        uint64_t mIndexOffset = 0;
        eastl::vector<FArdaRHIViewport> mViewports;
        eastl::vector<FArdaRHIRect> mScissors;
    };

    struct FArdaNativeComputeState
    {
        FArdaNativeObjectRef mPipeline;
        eastl::vector<FArdaNativeObjectRef> mBindings;
    };

    class IArdaNativeCommandList
    {
    public:
        virtual ~IArdaNativeCommandList() = default;
        virtual FArdaRHIStatus Open() = 0;
        virtual FArdaRHIStatus Close() = 0;
        virtual FArdaRHIStatus Reset() = 0;
        virtual FArdaRHIStatus WriteBuffer(
            const FArdaNativeObjectRef& Buffer,
            const FArdaRHIBufferDesc& Desc,
            const void* Data,
            size_t Size,
            uint64_t Offset) = 0;
        virtual FArdaRHIStatus CopyBuffer(
            const FArdaNativeObjectRef& Destination,
            uint64_t DestinationOffset,
            const FArdaNativeObjectRef& Source,
            uint64_t SourceOffset,
            uint64_t Size) = 0;
        virtual FArdaRHIStatus ClearTexture(
            const FArdaNativeObjectRef& Texture,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& Range,
            const FArdaRHIColor& Color) = 0;
        virtual FArdaRHIStatus ClearDepthStencilTexture(
            const FArdaNativeObjectRef& Texture,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& Range,
            bool bClearDepth,
            float Depth,
            bool bClearStencil,
            uint8_t Stencil) = 0;
        virtual FArdaRHIStatus SetTextureState(
            const FArdaNativeObjectRef& Texture,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& Range,
            EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus SetBufferState(
            const FArdaNativeObjectRef& Buffer,
            const FArdaRHIBufferDesc& Desc,
            EArdaRHIResourceState State) = 0;
        virtual void SetAutomaticBarriers(bool bEnabled) = 0;
        virtual FArdaRHIStatus BeginTrackingTextureState(
            const FArdaNativeObjectRef& Texture,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& Range,
            EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus BeginTrackingBufferState(
            const FArdaNativeObjectRef& Buffer,
            const FArdaRHIBufferDesc& Desc,
            EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus SetUAVBarriersForTexture(
            const FArdaNativeObjectRef& Texture,
            bool bEnabled) = 0;
        virtual FArdaRHIStatus SetUAVBarriersForBuffer(
            const FArdaNativeObjectRef& Buffer,
            bool bEnabled) = 0;
        virtual void CommitBarriers() = 0;
        virtual FArdaRHIStatus SetGraphicsState(
            const FArdaNativeGraphicsState& State) = 0;
        virtual FArdaRHIStatus SetComputeState(
            const FArdaNativeComputeState& State) = 0;
        virtual void SetPushConstants(const void* Data, size_t Size) = 0;
        virtual void Draw(const FArdaRHIDrawArguments& Arguments) = 0;
        virtual void DrawIndexed(const FArdaRHIDrawArguments& Arguments) = 0;
        virtual void Dispatch(uint32_t GroupsX, uint32_t GroupsY, uint32_t GroupsZ) = 0;
        virtual void BeginMarker(const char* Name) = 0;
        virtual void EndMarker() = 0;
    };

    class IArdaNativeApiDevice
    {
    public:
        virtual ~IArdaNativeApiDevice() = default;
        [[nodiscard]] virtual const FArdaRHICapabilities& GetCapabilities() const noexcept = 0;
        [[nodiscard]] virtual EArdaRHINativeResourceType GetTextureImportType() const noexcept = 0;
        [[nodiscard]] virtual EArdaRHINativeResourceType GetBufferImportType() const noexcept = 0;

        [[nodiscard]] virtual FArdaNativeObjectResult CreateTexture(
            const FArdaRHITextureDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaNativeObjectResult CreateBuffer(
            const FArdaRHIBufferDesc& Desc) = 0;
        /** Maps a host-visible native buffer range. */
        [[nodiscard]] virtual TArdaRHIResult<void*> MapBuffer(
            const FArdaNativeObjectRef& Buffer,
            uint64_t Offset,
            size_t Size) = 0;
        /** Unmaps a native buffer previously returned by MapBuffer. */
        virtual void UnmapBuffer(const FArdaNativeObjectRef& Buffer) noexcept = 0;
        [[nodiscard]] virtual FArdaNativeObjectResult ImportTexture(
            const FArdaRHINativeTextureImportDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaNativeObjectResult ImportBuffer(
            const FArdaRHINativeBufferImportDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaNativeObjectResult CreateSampler(
            const FArdaRHISamplerDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaNativeObjectResult CreateShader(
            const FArdaRHIShaderDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaNativeObjectResult CreateBindingLayout(
            const FArdaRHIBindingLayoutDesc& Desc) = 0;
        [[nodiscard]] virtual FArdaNativeObjectResult CreateBindingSet(
            const FArdaRHIBindingSetDesc& Desc,
            const FArdaNativeObjectRef& Layout,
            const eastl::vector<FArdaNativeBinding>& Bindings) = 0;
        [[nodiscard]] virtual FArdaNativeObjectResult CreateFramebuffer(
            const FArdaNativeFramebufferCreateInfo& Info) = 0;
        [[nodiscard]] virtual FArdaNativeObjectResult CreateGraphicsPipeline(
            const FArdaNativeGraphicsPipelineCreateInfo& Info) = 0;
        [[nodiscard]] virtual FArdaNativeObjectResult CreateComputePipeline(
            const FArdaNativeComputePipelineCreateInfo& Info) = 0;
        [[nodiscard]] virtual TArdaRHIResult<eastl::unique_ptr<IArdaNativeCommandList>>
            CreateCommandList(EArdaRHIQueueType Queue, bool bImmediate) = 0;
        [[nodiscard]] virtual TArdaRHIResult<uint64_t> ExecuteCommandList(
            IArdaNativeCommandList& CommandList,
            EArdaRHIQueueType Queue) = 0;
        /** Waits only for the requested submission when the API supports it. */
        virtual FArdaRHIStatus WaitForSubmission(uint64_t Submission)
        {
            (void)Submission;
            return WaitForIdle();
        }
        virtual FArdaRHIStatus WaitForIdle() = 0;
        virtual void RunGarbageCollection() = 0;
        [[nodiscard]] virtual FArdaNativeLifetimeStats
            GetLifetimeStats() const noexcept { return {}; }
        virtual void FlushPipelineCache() noexcept = 0;
    };

    /** Creates the provider-neutral RHI facade around one native API device. */
    [[nodiscard]] FArdaRHIDeviceRef CreateArdaNativeRHIDevice(
        eastl::shared_ptr<IArdaNativeApiDevice> Device);
}
