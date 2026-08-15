/** @file ArdaRHIDevice.h
 * Declares the RHI device and command-list interfaces used to create and submit GPU work.
 */

#pragma once

#include "ArdaRHICapabilities.h"
#include "ArdaRHIResources.h"

namespace arda::rhi
{
    /** Describes staging texture mapping. */
    struct FArdaRHIStagingTextureMapping
    {
        /** Stores the data. */
        void* mData = nullptr;
        /** Stores the row pitch. */
        size_t mRowPitch = 0;
    };

    /** Sizes of the bounded descriptor caches owned by a device. */
    struct FArdaRHICacheStats
    {
        /** Stores the samplers. */
        size_t mSamplers = 0;
        /** Stores the binding layouts. */
        size_t mBindingLayouts = 0;
        /** Stores the input layouts. */
        size_t mInputLayouts = 0;
        /** Stores the graphics pipelines. */
        size_t mGraphicsPipelines = 0;
        /** Stores the compute pipelines. */
        size_t mComputePipelines = 0;
        /** Stores the meshlet pipelines. */
        size_t mMeshletPipelines = 0;
        /** Stores the ray tracing pipelines. */
        size_t mRayTracingPipelines = 0;
        /** Stores the raster states. */
        size_t mRasterStates = 0;
        /** Stores the blend states. */
        size_t mBlendStates = 0;
        /** Stores the depth stencil states. */
        size_t mDepthStencilStates = 0;
    };

    /** Interface for command list. */
    class IArdaRHICommandList : public virtual IArdaRHIResource
    {
    public:
        /**
         * Opaque device that created this command list.
         * @return The requested object pointer.
         */
        [[nodiscard]] virtual IArdaRHIDevice* GetDevice() const noexcept = 0;
        /**
         * Returns the queue type.
         * @return The requested value.
         */
        [[nodiscard]] virtual EArdaRHIQueueType GetQueueType() const noexcept = 0;
        /**
         * Performs the open operation.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus Open() = 0;
        /**
         * Performs the close operation.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus Close() = 0;
        /**
         * Reopens a closed command list and discards its previous recording.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus Reset() = 0;
        /**
         * Performs the write buffer operation.
         * @param Buffer The buffer.
         * @param Data The data.
         * @param Size The size.
         * @param Offset The offset.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus WriteBuffer(IArdaRHIBuffer& Buffer, const void* Data, size_t Size, uint64_t Offset = 0) = 0;
        /**
         * Performs the copy buffer operation.
         * @param Destination The destination.
         * @param DestinationOffset The destination offset.
         * @param Source The source.
         * @param SourceOffset The source offset.
         * @param Size The size.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus CopyBuffer(IArdaRHIBuffer& Destination, uint64_t DestinationOffset, IArdaRHIBuffer& Source, uint64_t SourceOffset, uint64_t Size) = 0;
        /**
         * Performs the copy texture to staging operation.
         * @param Destination The destination.
         * @param DestinationSlice The destination slice.
         * @param Source The source.
         * @param SourceSlice The source slice.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus CopyTextureToStaging(IArdaRHIStagingTexture& Destination, const FArdaRHITextureSlice& DestinationSlice, IArdaRHITexture& Source, const FArdaRHITextureSlice& SourceSlice) = 0;
        /**
         * Performs the copy texture from staging operation.
         * @param Destination The destination.
         * @param DestinationSlice The destination slice.
         * @param Source The source.
         * @param SourceSlice The source slice.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus CopyTextureFromStaging(IArdaRHITexture& Destination, const FArdaRHITextureSlice& DestinationSlice, IArdaRHIStagingTexture& Source, const FArdaRHITextureSlice& SourceSlice) = 0;
        /**
         * Performs the clear texture operation.
         * @param Texture The texture.
         * @param Range The range.
         * @param Color The color.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus ClearTexture(IArdaRHITexture& Texture, const FArdaRHITextureSubresourceRange& Range, const FArdaRHIColor& Color) = 0;
        /**
         * Performs the set texture state operation.
         * @param Texture The texture.
         * @param Range The range.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetTextureState(IArdaRHITexture& Texture, const FArdaRHITextureSubresourceRange& Range, EArdaRHIResourceState State) = 0;
        /**
         * Performs the set buffer state operation.
         * @param Buffer The buffer.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetBufferState(IArdaRHIBuffer& Buffer, EArdaRHIResourceState State) = 0;
        /**
         * Performs the set accel struct state operation.
         * @param AccelStruct The accel struct.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetAccelStructState(IArdaRHIAccelStruct& AccelStruct, EArdaRHIResourceState State) = 0;
        /**
         * Performs the set automatic barriers operation.
         * @param bEnabled The b enabled.
         */
        virtual void SetAutomaticBarriers(bool bEnabled) = 0;
        /**
         * Performs the begin tracking texture state operation.
         * @param Texture The texture.
         * @param Range The range.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus BeginTrackingTextureState(IArdaRHITexture& Texture, const FArdaRHITextureSubresourceRange& Range, EArdaRHIResourceState State) = 0;
        /**
         * Performs the begin tracking buffer state operation.
         * @param Buffer The buffer.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus BeginTrackingBufferState(IArdaRHIBuffer& Buffer, EArdaRHIResourceState State) = 0;
        /**
         * Performs the set UAVbarriers for texture operation.
         * @param Texture The texture.
         * @param bEnabled The b enabled.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetUAVBarriersForTexture(IArdaRHITexture& Texture, bool bEnabled) = 0;
        /**
         * Performs the set UAVbarriers for buffer operation.
         * @param Buffer The buffer.
         * @param bEnabled The b enabled.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetUAVBarriersForBuffer(IArdaRHIBuffer& Buffer, bool bEnabled) = 0;
        /** Performs the commit barriers operation. */
        virtual void CommitBarriers() = 0;
        /**
         * Performs the clear texture uint operation.
         * @param Texture The texture.
         * @param Range The range.
         * @param Value The value.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus ClearTextureUInt(IArdaRHITexture& Texture, const FArdaRHITextureSubresourceRange& Range, uint32_t Value) = 0;
        /**
         * Performs the clear depth stencil texture operation.
         * @param Texture The texture.
         * @param Range The range.
         * @param bClearDepth The b clear depth.
         * @param Depth The depth.
         * @param bClearStencil The b clear stencil.
         * @param Stencil The stencil.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus ClearDepthStencilTexture(IArdaRHITexture& Texture, const FArdaRHITextureSubresourceRange& Range, bool bClearDepth, float Depth, bool bClearStencil, uint8_t Stencil) = 0;
        /**
         * Performs the clear buffer uint operation.
         * @param Buffer The buffer.
         * @param Value The value.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus ClearBufferUInt(IArdaRHIBuffer& Buffer, uint32_t Value) = 0;
        /**
         * Performs the set graphics state operation.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetGraphicsState(const FArdaRHIGraphicsState& State) = 0;
        /**
         * Performs the set compute state operation.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetComputeState(const FArdaRHIComputeState& State) = 0;
        /**
         * Performs the set meshlet state operation.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetMeshletState(const FArdaRHIMeshletState& State) = 0;
        /**
         * Performs the set ray tracing state operation.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetRayTracingState(const FArdaRHIRayTracingState& State) = 0;
        /**
         * Performs the set push constants operation.
         * @param Data The data.
         * @param Size The size.
         */
        virtual void SetPushConstants(const void* Data, size_t Size) = 0;
        /**
         * Performs the draw operation.
         * @param Arguments The arguments.
         */
        virtual void Draw(const FArdaRHIDrawArguments& Arguments) = 0;
        /**
         * Performs the draw indexed operation.
         * @param Arguments The arguments.
         */
        virtual void DrawIndexed(const FArdaRHIDrawArguments& Arguments) = 0;
        /**
         * Performs the dispatch operation.
         * @param GroupsX The groups x.
         * @param GroupsY The groups y.
         * @param GroupsZ The groups z.
         */
        virtual void Dispatch(uint32_t GroupsX, uint32_t GroupsY = 1, uint32_t GroupsZ = 1) = 0;
        /**
         * Performs the dispatch mesh operation.
         * @param GroupsX The groups x.
         * @param GroupsY The groups y.
         * @param GroupsZ The groups z.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus DispatchMesh(uint32_t GroupsX, uint32_t GroupsY = 1, uint32_t GroupsZ = 1) = 0;
        /**
         * Performs the dispatch rays operation.
         * @param Width The width.
         * @param Height The height.
         * @param Depth The depth.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus DispatchRays(uint32_t Width, uint32_t Height = 1, uint32_t Depth = 1) = 0;
        /**
         * Performs the build bottom level accel struct operation.
         * @param AccelStruct The accel struct.
         * @param Geometries The geometries.
         * @param Flags The flags.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus BuildBottomLevelAccelStruct(IArdaRHIAccelStruct& AccelStruct, const eastl::vector<FArdaRHIRayTracingGeometryDesc>& Geometries, EArdaRHIAccelStructBuildFlags Flags) = 0;
        /**
         * Performs the build top level accel struct operation.
         * @param AccelStruct The accel struct.
         * @param Instances The instances.
         * @param Flags The flags.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus BuildTopLevelAccelStruct(IArdaRHIAccelStruct& AccelStruct, const eastl::vector<FArdaRHIRayTracingInstanceDesc>& Instances, EArdaRHIAccelStructBuildFlags Flags) = 0;
        /**
         * Performs the build top level accel struct from buffer operation.
         * @param AccelStruct The accel struct.
         * @param InstanceBuffer The instance buffer.
         * @param Offset The offset.
         * @param InstanceCount The instance count.
         * @param Flags The flags.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus BuildTopLevelAccelStructFromBuffer(IArdaRHIAccelStruct& AccelStruct, IArdaRHIBuffer& InstanceBuffer, uint64_t Offset, size_t InstanceCount, EArdaRHIAccelStructBuildFlags Flags) = 0;
        /**
         * Performs the build opacity micromap operation.
         * @param Micromap The micromap.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus BuildOpacityMicromap(IArdaRHIOpacityMicromap& Micromap) = 0;
        /**
         * Performs the clear sampler feedback texture operation.
         * @param Texture The texture.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus ClearSamplerFeedbackTexture(IArdaRHISamplerFeedbackTexture& Texture) = 0;
        /**
         * Performs the decode sampler feedback texture operation.
         * @param Destination The destination.
         * @param Texture The texture.
         * @param Format The format.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus DecodeSamplerFeedbackTexture(IArdaRHIBuffer& Destination, IArdaRHISamplerFeedbackTexture& Texture, EArdaRHIFormat Format) = 0;
        /**
         * Performs the set sampler feedback texture state operation.
         * @param Texture The texture.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetSamplerFeedbackTextureState(IArdaRHISamplerFeedbackTexture& Texture, EArdaRHIResourceState State) = 0;
        /**
         * Performs the begin timer query operation.
         * @param Query The query.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus BeginTimerQuery(IArdaRHITimerQuery& Query) = 0;
        /**
         * Performs the end timer query operation.
         * @param Query The query.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus EndTimerQuery(IArdaRHITimerQuery& Query) = 0;
        /**
         * Performs the begin marker operation.
         * @param Name The name.
         */
        virtual void BeginMarker(const char* Name) = 0;
        /** Performs the end marker operation. */
        virtual void EndMarker() = 0;
    };

    /** Interface for device. */
    class IArdaRHIDevice : public virtual IArdaRHIResource
    {
    public:
        /**
         * Returns the capabilities.
         * @return A reference to the requested value.
         */
        [[nodiscard]] virtual const FArdaRHICapabilities& GetCapabilities() const noexcept = 0;
        /**
         * Creates a texture.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureRef> CreateTexture(const FArdaRHITextureDesc& Desc) = 0;
        /**
         * Creates a texture reference.
         * @param Texture The texture.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureReferenceRef> CreateTextureReference(const FArdaRHITextureRef& Texture = {}) = 0;
        /**
         * Performs the set texture reference operation.
         * @param Reference The reference.
         * @param Texture The texture.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetTextureReference(const FArdaRHITextureReferenceRef& Reference, const FArdaRHITextureRef& Texture) = 0;
        /**
         * Creates a buffer.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBufferRef> CreateBuffer(const FArdaRHIBufferDesc& Desc) = 0;
        /**
         * Creates a uniform buffer.
         * @param Desc The desc.
         * @param InitialData The initial data.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIUniformBufferRef> CreateUniformBuffer(const FArdaRHIUniformBufferDesc& Desc, const void* InitialData = nullptr) = 0;
        /**
         * Performs the import native texture operation.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureRef> ImportNativeTexture(const FArdaRHINativeTextureImportDesc& Desc) = 0;
        /**
         * Performs the import native buffer operation.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBufferRef> ImportNativeBuffer(const FArdaRHINativeBufferImportDesc& Desc) = 0;
        /**
         * Creates a heap.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIHeapRef> CreateHeap(const FArdaRHIHeapDesc& Desc) = 0;
        /**
         * Creates a staging texture.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIStagingTextureRef> CreateStagingTexture(const FArdaRHIStagingTextureDesc& Desc) = 0;
        /**
         * Performs the map staging texture operation.
         * @param Texture The texture.
         * @param Slice The slice.
         * @param Access The access.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaRHIStagingTextureRef& Texture, const FArdaRHITextureSlice& Slice, EArdaRHICpuAccess Access) = 0;
        /**
         * Performs the unmap staging texture operation.
         * @param Texture The texture.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus UnmapStagingTexture(const FArdaRHIStagingTextureRef& Texture) = 0;
        /**
         * Creates a shader resource view.
         * @param Resource The resource.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderResourceViewRef> CreateShaderResourceView(const TArdaRHIRef<IArdaRHIResource>& Resource, const FArdaRHIViewDesc& Desc) = 0;
        /**
         * Creates a unordered access view.
         * @param Resource The resource.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIUnorderedAccessViewRef> CreateUnorderedAccessView(const TArdaRHIRef<IArdaRHIResource>& Resource, const FArdaRHIViewDesc& Desc) = 0;
        /**
         * Creates a sampler.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHISamplerRef> CreateSampler(const FArdaRHISamplerDesc& Desc) = 0;
        /**
         * Creates a shader.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderRef> CreateShader(const FArdaRHIShaderDesc& Desc) = 0;
        /**
         * Creates a shader library.
         * @param Bytecode The bytecode.
         * @param BytecodeSize The bytecode size.
         * @param DebugName The debug name.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderLibraryRef> CreateShaderLibrary(const void* Bytecode, size_t BytecodeSize, const char* DebugName = nullptr) = 0;
        /**
         * Returns the shader from library.
         * @param Library The library.
         * @param EntryPoint The entry point.
         * @param Stage The stage.
         * @param DebugName The debug name.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderRef> GetShaderFromLibrary(const FArdaRHIShaderLibraryRef& Library, const char* EntryPoint, EArdaRHIShaderStage Stage, const char* DebugName = nullptr) = 0;
        /**
         * Creates a input layout.
         * @param Attributes The attributes.
         * @param VertexShader The vertex shader.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIInputLayoutRef> CreateInputLayout(const eastl::vector<FArdaRHIVertexAttributeDesc>& Attributes, const FArdaRHIShaderRef& VertexShader = {}) = 0;
        /**
         * Creates a binding layout.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindingLayout(const FArdaRHIBindingLayoutDesc& Desc) = 0;
        /**
         * Creates a bindless layout.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindlessLayout(const FArdaRHIBindlessLayoutDesc& Desc) = 0;
        /**
         * Creates a binding set.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBindingSetRef> CreateBindingSet(const FArdaRHIBindingSetDesc& Desc) = 0;
        /**
         * Creates a descriptor table.
         * @param Layout The layout.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIDescriptorTableRef> CreateDescriptorTable(const FArdaRHIBindingLayoutRef& Layout) = 0;
        /**
         * Performs the resize descriptor table operation.
         * @param Table The table.
         * @param NewSize The new size.
         * @param bKeepContents The b keep contents.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus ResizeDescriptorTable(const FArdaRHIDescriptorTableRef& Table, uint32_t NewSize, bool bKeepContents = true) = 0;
        /**
         * Performs the write descriptor table operation.
         * @param Table The table.
         * @param Item The item.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus WriteDescriptorTable(const FArdaRHIDescriptorTableRef& Table, const FArdaRHIBindingItem& Item) = 0;
        /**
         * Creates a framebuffer.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIFramebufferRef> CreateFramebuffer(const FArdaRHIFramebufferDesc& Desc) = 0;
        /**
         * Creates a graphics pipeline.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIGraphicsPipelineRef> CreateGraphicsPipeline(const FArdaRHIGraphicsPipelineDesc& Desc) = 0;
        /**
         * Creates a compute pipeline.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIComputePipelineRef> CreateComputePipeline(const FArdaRHIComputePipelineDesc& Desc) = 0;
        /**
         * Creates a meshlet pipeline.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIMeshletPipelineRef> CreateMeshletPipeline(const FArdaRHIMeshletPipelineDesc& Desc) = 0;
        /**
         * Creates a raster state.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIRasterStateRef> CreateRasterState(const FArdaRHIRasterState& Desc) = 0;
        /**
         * Creates a blend state.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIBlendStateRef> CreateBlendState(const FArdaRHIBlendState& Desc) = 0;
        /**
         * Creates a depth stencil state.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIDepthStencilStateRef> CreateDepthStencilState(const FArdaRHIDepthStencilState& Desc) = 0;
        /**
         * Creates a accel struct.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIAccelStructRef> CreateAccelStruct(const FArdaRHIAccelStructDesc& Desc) = 0;
        /**
         * Creates a opacity micromap.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIOpacityMicromapRef> CreateOpacityMicromap(const FArdaRHIOpacityMicromapDesc& Desc) = 0;
        /**
         * Creates a ray tracing pipeline.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIRayTracingPipelineRef> CreateRayTracingPipeline(const FArdaRHIRayTracingPipelineDesc& Desc) = 0;
        /**
         * Creates a shader table.
         * @param Pipeline The pipeline.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderTableRef> CreateShaderTable(const FArdaRHIRayTracingPipelineRef& Pipeline, const FArdaRHIShaderTableDesc& Desc) = 0;
        /**
         * Performs the set shader table ray generation operation.
         * @param Table The table.
         * @param ExportName The export name.
         * @param Bindings The bindings.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SetShaderTableRayGeneration(const FArdaRHIShaderTableRef& Table, const char* ExportName, const FArdaRHIBindingSetRef& Bindings = {}) = 0;
        /**
         * Performs the add shader table miss operation.
         * @param Table The table.
         * @param ExportName The export name.
         * @param Bindings The bindings.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<int> AddShaderTableMiss(const FArdaRHIShaderTableRef& Table, const char* ExportName, const FArdaRHIBindingSetRef& Bindings = {}) = 0;
        /**
         * Performs the add shader table hit group operation.
         * @param Table The table.
         * @param ExportName The export name.
         * @param Bindings The bindings.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<int> AddShaderTableHitGroup(const FArdaRHIShaderTableRef& Table, const char* ExportName, const FArdaRHIBindingSetRef& Bindings = {}) = 0;
        /**
         * Performs the add shader table callable operation.
         * @param Table The table.
         * @param ExportName The export name.
         * @param Bindings The bindings.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<int> AddShaderTableCallable(const FArdaRHIShaderTableRef& Table, const char* ExportName, const FArdaRHIBindingSetRef& Bindings = {}) = 0;
        /**
         * Creates a sampler feedback texture.
         * @param PairedTexture The paired texture.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHISamplerFeedbackTextureRef> CreateSamplerFeedbackTexture(const FArdaRHITextureRef& PairedTexture, const FArdaRHISamplerFeedbackTextureDesc& Desc) = 0;
        /**
         * Creates a event query.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIEventQueryRef> CreateEventQuery() = 0;
        /**
         * Creates a timer query.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITimerQueryRef> CreateTimerQuery() = 0;
        /**
         * Creates a GPU fence.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIGpuFenceRef> CreateGpuFence() = 0;
        /**
         * Performs the signal event query operation.
         * @param Query The query.
         * @param Queue The queue.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SignalEventQuery(const FArdaRHIEventQueryRef& Query, EArdaRHIQueueType Queue) = 0;
        /**
         * Performs the poll event query operation.
         * @param Query The query.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<bool> PollEventQuery(const FArdaRHIEventQueryRef& Query) = 0;
        /**
         * Performs the wait event query operation.
         * @param Query The query.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus WaitEventQuery(const FArdaRHIEventQueryRef& Query) = 0;
        /**
         * Performs the reset event query operation.
         * @param Query The query.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus ResetEventQuery(const FArdaRHIEventQueryRef& Query) = 0;
        /**
         * Performs the poll timer query operation.
         * @param Query The query.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<bool> PollTimerQuery(const FArdaRHITimerQueryRef& Query) = 0;
        /**
         * Returns the timer query seconds.
         * @param Query The query.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<float> GetTimerQuerySeconds(const FArdaRHITimerQueryRef& Query) = 0;
        /**
         * Performs the reset timer query operation.
         * @param Query The query.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus ResetTimerQuery(const FArdaRHITimerQueryRef& Query) = 0;
        /**
         * Performs the signal GPU fence operation.
         * @param Fence The fence.
         * @param Queue The queue.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus SignalGpuFence(const FArdaRHIGpuFenceRef& Fence, EArdaRHIQueueType Queue) = 0;
        /**
         * Performs the poll GPU fence operation.
         * @param Fence The fence.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<bool> PollGpuFence(const FArdaRHIGpuFenceRef& Fence) = 0;
        /**
         * Performs the wait GPU fence operation.
         * @param Fence The fence.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus WaitGpuFence(const FArdaRHIGpuFenceRef& Fence) = 0;
        /**
         * Performs the reset GPU fence operation.
         * @param Fence The fence.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus ResetGpuFence(const FArdaRHIGpuFenceRef& Fence) = 0;
        /**
         * Creates a command list.
         * @param Queue The queue.
         * @param bImmediateExecution The b immediate execution.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHICommandListRef> CreateCommandList(
            EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics,
            bool bImmediateExecution = false) = 0;
        /**
         * Performs the execute command list operation.
         * @param CommandList The command list.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<uint64_t> ExecuteCommandList(const FArdaRHICommandListRef& CommandList) = 0;
        /**
         * Performs the execute command lists operation.
         * @param CommandLists The command lists.
         * @param Queue The queue.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<uint64_t> ExecuteCommandLists(const eastl::vector<FArdaRHICommandListRef>& CommandLists, EArdaRHIQueueType Queue) = 0;
        /**
         * Performs the queue wait operation.
         * @param WaitQueue The wait queue.
         * @param ExecutionQueue The execution queue.
         * @param Instance The instance.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus QueueWait(EArdaRHIQueueType WaitQueue, EArdaRHIQueueType ExecutionQueue, uint64_t Instance) = 0;
        /**
         * Returns the texture memory requirements.
         * @param Texture The texture.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaRHITextureRef& Texture) = 0;
        /**
         * Returns the buffer memory requirements.
         * @param Buffer The buffer.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaRHIBufferRef& Buffer) = 0;
        /**
         * Returns the accel struct memory requirements.
         * @param AccelStruct The accel struct.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetAccelStructMemoryRequirements(const FArdaRHIAccelStructRef& AccelStruct) = 0;
        /**
         * Performs the bind texture memory operation.
         * @param Texture The texture.
         * @param Heap The heap.
         * @param Offset The offset.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus BindTextureMemory(const FArdaRHITextureRef& Texture, const FArdaRHIHeapRef& Heap, uint64_t Offset) = 0;
        /**
         * Performs the bind buffer memory operation.
         * @param Buffer The buffer.
         * @param Heap The heap.
         * @param Offset The offset.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus BindBufferMemory(const FArdaRHIBufferRef& Buffer, const FArdaRHIHeapRef& Heap, uint64_t Offset) = 0;
        /**
         * Performs the bind accel struct memory operation.
         * @param AccelStruct The accel struct.
         * @param Heap The heap.
         * @param Offset The offset.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus BindAccelStructMemory(const FArdaRHIAccelStructRef& AccelStruct, const FArdaRHIHeapRef& Heap, uint64_t Offset) = 0;
        /**
         * Returns the texture tiling.
         * @param Texture The texture.
         * @return The requested value and its operation status.
         */
        [[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureTiling> GetTextureTiling(const FArdaRHITextureRef& Texture) = 0;
        /**
         * Performs the update texture tile mappings operation.
         * @param Texture The texture.
         * @param Mappings The mappings.
         * @param Queue The queue.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus UpdateTextureTileMappings(const FArdaRHITextureRef& Texture, const eastl::vector<FArdaRHITextureTileMapping>& Mappings, EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics) = 0;
        /**
         * Performs the query work graph support operation.
         * @return A status describing whether the operation succeeded.
         */
        [[nodiscard]] virtual FArdaRHIStatus QueryWorkGraphSupport() const = 0;
        /**
         * Performs the query shader bundle support operation.
         * @return A status describing whether the operation succeeded.
         */
        [[nodiscard]] virtual FArdaRHIStatus QueryShaderBundleSupport() const = 0;
        /**
         * Performs the query custom present support operation.
         * @return A status describing whether the operation succeeded.
         */
        [[nodiscard]] virtual FArdaRHIStatus QueryCustomPresentSupport() const = 0;
        /**
         * Performs the query stream source support operation.
         * @return A status describing whether the operation succeeded.
         */
        [[nodiscard]] virtual FArdaRHIStatus QueryStreamSourceSupport() const = 0;
        /** Evicts all descriptor-cached objects; outstanding caller references remain valid. */
        virtual void TrimDescriptorCaches() = 0;
        /**
         * Returns the descriptor cache stats.
         * @return The requested value.
         */
        [[nodiscard]] virtual FArdaRHICacheStats GetDescriptorCacheStats() const noexcept = 0;
        /**
         * Performs the wait for idle operation.
         * @return A status describing whether the operation succeeded.
         */
        virtual FArdaRHIStatus WaitForIdle() = 0;
        /** Performs the run garbage collection operation. */
        virtual void RunGarbageCollection() = 0;
    };
}
