#pragma once

#include "ArdaRHICapabilities.h"
#include "ArdaRHIResources.h"

namespace arda::rhi
{
    struct FArdaRHIStagingTextureMapping
    {
        void* mData = nullptr;
        size_t mRowPitch = 0;
    };

    /** Sizes of the bounded descriptor caches owned by a device. */
    struct FArdaRHICacheStats
    {
        size_t mSamplers = 0;
        size_t mBindingLayouts = 0;
        size_t mInputLayouts = 0;
        size_t mGraphicsPipelines = 0;
        size_t mComputePipelines = 0;
        size_t mMeshletPipelines = 0;
        size_t mRayTracingPipelines = 0;
        size_t mRasterStates = 0;
        size_t mBlendStates = 0;
        size_t mDepthStencilStates = 0;
    };

    class IArdaRHICommandList : public virtual IArdaRHIResource
    {
    public:
        /** Opaque device that created this command list. */
        [[nodiscard]] virtual IArdaRHIDevice* GetDevice() const noexcept = 0;
        [[nodiscard]] virtual EArdaRHIQueueType GetQueueType() const noexcept = 0;
        virtual FArdaRHIStatus Open() = 0;
        virtual FArdaRHIStatus Close() = 0;
        /** Reopens a closed command list and discards its previous recording. */
        virtual FArdaRHIStatus Reset() = 0;
        virtual FArdaRHIStatus WriteBuffer(IArdaRHIBuffer& Buffer, const void* Data, size_t Size, uint64_t Offset = 0) = 0;
        virtual FArdaRHIStatus CopyBuffer(IArdaRHIBuffer& Destination, uint64_t DestinationOffset, IArdaRHIBuffer& Source, uint64_t SourceOffset, uint64_t Size) = 0;
        virtual FArdaRHIStatus CopyTextureToStaging(IArdaRHIStagingTexture& Destination, const FArdaRHITextureSlice& DestinationSlice, IArdaRHITexture& Source, const FArdaRHITextureSlice& SourceSlice) = 0;
        virtual FArdaRHIStatus CopyTextureFromStaging(IArdaRHITexture& Destination, const FArdaRHITextureSlice& DestinationSlice, IArdaRHIStagingTexture& Source, const FArdaRHITextureSlice& SourceSlice) = 0;
        virtual FArdaRHIStatus ClearTexture(IArdaRHITexture& Texture, const FArdaRHITextureSubresourceRange& Range, const FArdaRHIColor& Color) = 0;
        virtual FArdaRHIStatus SetTextureState(IArdaRHITexture& Texture, const FArdaRHITextureSubresourceRange& Range, EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus SetBufferState(IArdaRHIBuffer& Buffer, EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus SetAccelStructState(IArdaRHIAccelStruct& AccelStruct, EArdaRHIResourceState State) = 0;
        virtual void SetAutomaticBarriers(bool bEnabled) = 0;
        virtual FArdaRHIStatus BeginTrackingTextureState(IArdaRHITexture& Texture, const FArdaRHITextureSubresourceRange& Range, EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus BeginTrackingBufferState(IArdaRHIBuffer& Buffer, EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus SetUAVBarriersForTexture(IArdaRHITexture& Texture, bool bEnabled) = 0;
        virtual FArdaRHIStatus SetUAVBarriersForBuffer(IArdaRHIBuffer& Buffer, bool bEnabled) = 0;
        virtual void CommitBarriers() = 0;
        virtual FArdaRHIStatus ClearTextureUInt(IArdaRHITexture& Texture, const FArdaRHITextureSubresourceRange& Range, uint32_t Value) = 0;
        virtual FArdaRHIStatus ClearDepthStencilTexture(IArdaRHITexture& Texture, const FArdaRHITextureSubresourceRange& Range, bool bClearDepth, float Depth, bool bClearStencil, uint8_t Stencil) = 0;
        virtual FArdaRHIStatus ClearBufferUInt(IArdaRHIBuffer& Buffer, uint32_t Value) = 0;
        virtual FArdaRHIStatus SetGraphicsState(const FArdaRHIGraphicsState& State) = 0;
        virtual FArdaRHIStatus SetComputeState(const FArdaRHIComputeState& State) = 0;
        virtual FArdaRHIStatus SetMeshletState(const FArdaRHIMeshletState& State) = 0;
        virtual FArdaRHIStatus SetRayTracingState(const FArdaRHIRayTracingState& State) = 0;
        virtual void SetPushConstants(const void* Data, size_t Size) = 0;
        virtual void Draw(const FArdaRHIDrawArguments& Arguments) = 0;
        virtual void DrawIndexed(const FArdaRHIDrawArguments& Arguments) = 0;
        virtual void Dispatch(uint32_t GroupsX, uint32_t GroupsY = 1, uint32_t GroupsZ = 1) = 0;
        virtual FArdaRHIStatus DispatchMesh(uint32_t GroupsX, uint32_t GroupsY = 1, uint32_t GroupsZ = 1) = 0;
        virtual FArdaRHIStatus DispatchRays(uint32_t Width, uint32_t Height = 1, uint32_t Depth = 1) = 0;
        virtual FArdaRHIStatus BuildBottomLevelAccelStruct(IArdaRHIAccelStruct& AccelStruct, const eastl::vector<FArdaRHIRayTracingGeometryDesc>& Geometries, EArdaRHIAccelStructBuildFlags Flags) = 0;
        virtual FArdaRHIStatus BuildTopLevelAccelStruct(IArdaRHIAccelStruct& AccelStruct, const eastl::vector<FArdaRHIRayTracingInstanceDesc>& Instances, EArdaRHIAccelStructBuildFlags Flags) = 0;
        virtual FArdaRHIStatus BuildTopLevelAccelStructFromBuffer(IArdaRHIAccelStruct& AccelStruct, IArdaRHIBuffer& InstanceBuffer, uint64_t Offset, size_t InstanceCount, EArdaRHIAccelStructBuildFlags Flags) = 0;
        virtual FArdaRHIStatus BuildOpacityMicromap(IArdaRHIOpacityMicromap& Micromap) = 0;
        virtual FArdaRHIStatus ClearSamplerFeedbackTexture(IArdaRHISamplerFeedbackTexture& Texture) = 0;
        virtual FArdaRHIStatus DecodeSamplerFeedbackTexture(IArdaRHIBuffer& Destination, IArdaRHISamplerFeedbackTexture& Texture, EArdaRHIFormat Format) = 0;
        virtual FArdaRHIStatus SetSamplerFeedbackTextureState(IArdaRHISamplerFeedbackTexture& Texture, EArdaRHIResourceState State) = 0;
        virtual FArdaRHIStatus BeginTimerQuery(IArdaRHITimerQuery& Query) = 0;
        virtual FArdaRHIStatus EndTimerQuery(IArdaRHITimerQuery& Query) = 0;
        virtual void BeginMarker(const char* Name) = 0;
        virtual void EndMarker() = 0;
    };

    class IArdaRHIDevice : public virtual IArdaRHIResource
    {
    public:
        [[nodiscard]] virtual const FArdaRHICapabilities& GetCapabilities() const noexcept = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureRef> CreateTexture(const FArdaRHITextureDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureReferenceRef> CreateTextureReference(const FArdaRHITextureRef& Texture = {}) = 0;
        virtual FArdaRHIStatus SetTextureReference(const FArdaRHITextureReferenceRef& Reference, const FArdaRHITextureRef& Texture) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBufferRef> CreateBuffer(const FArdaRHIBufferDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIUniformBufferRef> CreateUniformBuffer(const FArdaRHIUniformBufferDesc& Desc, const void* InitialData = nullptr) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureRef> ImportNativeTexture(const FArdaRHINativeTextureImportDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBufferRef> ImportNativeBuffer(const FArdaRHINativeBufferImportDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIHeapRef> CreateHeap(const FArdaRHIHeapDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIStagingTextureRef> CreateStagingTexture(const FArdaRHIStagingTextureDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaRHIStagingTextureRef& Texture, const FArdaRHITextureSlice& Slice, EArdaRHICpuAccess Access) = 0;
        virtual FArdaRHIStatus UnmapStagingTexture(const FArdaRHIStagingTextureRef& Texture) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderResourceViewRef> CreateShaderResourceView(const TArdaRHIRef<IArdaRHIResource>& Resource, const FArdaRHIViewDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIUnorderedAccessViewRef> CreateUnorderedAccessView(const TArdaRHIRef<IArdaRHIResource>& Resource, const FArdaRHIViewDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHISamplerRef> CreateSampler(const FArdaRHISamplerDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderRef> CreateShader(const FArdaRHIShaderDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderLibraryRef> CreateShaderLibrary(const void* Bytecode, size_t BytecodeSize, const char* DebugName = nullptr) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderRef> GetShaderFromLibrary(const FArdaRHIShaderLibraryRef& Library, const char* EntryPoint, EArdaRHIShaderStage Stage, const char* DebugName = nullptr) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIInputLayoutRef> CreateInputLayout(const eastl::vector<FArdaRHIVertexAttributeDesc>& Attributes, const FArdaRHIShaderRef& VertexShader = {}) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindingLayout(const FArdaRHIBindingLayoutDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindlessLayout(const FArdaRHIBindlessLayoutDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBindingSetRef> CreateBindingSet(const FArdaRHIBindingSetDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIDescriptorTableRef> CreateDescriptorTable(const FArdaRHIBindingLayoutRef& Layout) = 0;
        virtual FArdaRHIStatus ResizeDescriptorTable(const FArdaRHIDescriptorTableRef& Table, uint32_t NewSize, bool bKeepContents = true) = 0;
        virtual FArdaRHIStatus WriteDescriptorTable(const FArdaRHIDescriptorTableRef& Table, const FArdaRHIBindingItem& Item) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIFramebufferRef> CreateFramebuffer(const FArdaRHIFramebufferDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIGraphicsPipelineRef> CreateGraphicsPipeline(const FArdaRHIGraphicsPipelineDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIComputePipelineRef> CreateComputePipeline(const FArdaRHIComputePipelineDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIMeshletPipelineRef> CreateMeshletPipeline(const FArdaRHIMeshletPipelineDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIRasterStateRef> CreateRasterState(const FArdaRHIRasterState& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBlendStateRef> CreateBlendState(const FArdaRHIBlendState& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIDepthStencilStateRef> CreateDepthStencilState(const FArdaRHIDepthStencilState& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIAccelStructRef> CreateAccelStruct(const FArdaRHIAccelStructDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIOpacityMicromapRef> CreateOpacityMicromap(const FArdaRHIOpacityMicromapDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIRayTracingPipelineRef> CreateRayTracingPipeline(const FArdaRHIRayTracingPipelineDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderTableRef> CreateShaderTable(const FArdaRHIRayTracingPipelineRef& Pipeline, const FArdaRHIShaderTableDesc& Desc) = 0;
        virtual FArdaRHIStatus SetShaderTableRayGeneration(const FArdaRHIShaderTableRef& Table, const char* ExportName, const FArdaRHIBindingSetRef& Bindings = {}) = 0;
        [[nodiscard]] virtual TArdaRHIResult<int> AddShaderTableMiss(const FArdaRHIShaderTableRef& Table, const char* ExportName, const FArdaRHIBindingSetRef& Bindings = {}) = 0;
        [[nodiscard]] virtual TArdaRHIResult<int> AddShaderTableHitGroup(const FArdaRHIShaderTableRef& Table, const char* ExportName, const FArdaRHIBindingSetRef& Bindings = {}) = 0;
        [[nodiscard]] virtual TArdaRHIResult<int> AddShaderTableCallable(const FArdaRHIShaderTableRef& Table, const char* ExportName, const FArdaRHIBindingSetRef& Bindings = {}) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHISamplerFeedbackTextureRef> CreateSamplerFeedbackTexture(const FArdaRHITextureRef& PairedTexture, const FArdaRHISamplerFeedbackTextureDesc& Desc) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIEventQueryRef> CreateEventQuery() = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITimerQueryRef> CreateTimerQuery() = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIGpuFenceRef> CreateGpuFence() = 0;
        virtual FArdaRHIStatus SignalEventQuery(const FArdaRHIEventQueryRef& Query, EArdaRHIQueueType Queue) = 0;
        [[nodiscard]] virtual TArdaRHIResult<bool> PollEventQuery(const FArdaRHIEventQueryRef& Query) = 0;
        virtual FArdaRHIStatus WaitEventQuery(const FArdaRHIEventQueryRef& Query) = 0;
        virtual FArdaRHIStatus ResetEventQuery(const FArdaRHIEventQueryRef& Query) = 0;
        [[nodiscard]] virtual TArdaRHIResult<bool> PollTimerQuery(const FArdaRHITimerQueryRef& Query) = 0;
        [[nodiscard]] virtual TArdaRHIResult<float> GetTimerQuerySeconds(const FArdaRHITimerQueryRef& Query) = 0;
        virtual FArdaRHIStatus ResetTimerQuery(const FArdaRHITimerQueryRef& Query) = 0;
        virtual FArdaRHIStatus SignalGpuFence(const FArdaRHIGpuFenceRef& Fence, EArdaRHIQueueType Queue) = 0;
        [[nodiscard]] virtual TArdaRHIResult<bool> PollGpuFence(const FArdaRHIGpuFenceRef& Fence) = 0;
        virtual FArdaRHIStatus WaitGpuFence(const FArdaRHIGpuFenceRef& Fence) = 0;
        virtual FArdaRHIStatus ResetGpuFence(const FArdaRHIGpuFenceRef& Fence) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHICommandListRef> CreateCommandList(
            EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics,
            bool bImmediateExecution = false) = 0;
        [[nodiscard]] virtual TArdaRHIResult<uint64_t> ExecuteCommandList(const FArdaRHICommandListRef& CommandList) = 0;
        [[nodiscard]] virtual TArdaRHIResult<uint64_t> ExecuteCommandLists(const eastl::vector<FArdaRHICommandListRef>& CommandLists, EArdaRHIQueueType Queue) = 0;
        virtual FArdaRHIStatus QueueWait(EArdaRHIQueueType WaitQueue, EArdaRHIQueueType ExecutionQueue, uint64_t Instance) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaRHITextureRef& Texture) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaRHIBufferRef& Buffer) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetAccelStructMemoryRequirements(const FArdaRHIAccelStructRef& AccelStruct) = 0;
        virtual FArdaRHIStatus BindTextureMemory(const FArdaRHITextureRef& Texture, const FArdaRHIHeapRef& Heap, uint64_t Offset) = 0;
        virtual FArdaRHIStatus BindBufferMemory(const FArdaRHIBufferRef& Buffer, const FArdaRHIHeapRef& Heap, uint64_t Offset) = 0;
        virtual FArdaRHIStatus BindAccelStructMemory(const FArdaRHIAccelStructRef& AccelStruct, const FArdaRHIHeapRef& Heap, uint64_t Offset) = 0;
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureTiling> GetTextureTiling(const FArdaRHITextureRef& Texture) = 0;
        virtual FArdaRHIStatus UpdateTextureTileMappings(const FArdaRHITextureRef& Texture, const eastl::vector<FArdaRHITextureTileMapping>& Mappings, EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics) = 0;
        [[nodiscard]] virtual FArdaRHIStatus QueryWorkGraphSupport() const = 0;
        [[nodiscard]] virtual FArdaRHIStatus QueryShaderBundleSupport() const = 0;
        [[nodiscard]] virtual FArdaRHIStatus QueryCustomPresentSupport() const = 0;
        [[nodiscard]] virtual FArdaRHIStatus QueryStreamSourceSupport() const = 0;
        /** Evicts all descriptor-cached objects; outstanding caller references remain valid. */
        virtual void TrimDescriptorCaches() = 0;
        [[nodiscard]] virtual FArdaRHICacheStats GetDescriptorCacheStats() const noexcept = 0;
        virtual FArdaRHIStatus WaitForIdle() = 0;
        virtual void RunGarbageCollection() = 0;
    };
}
