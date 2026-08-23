/** @file ArdaRHIFwd.h
 * Provides forward declarations and intrusive reference aliases for RHI interfaces.
 */

#pragma once

namespace arda::rhi
{
    /** Forward declaration of RHI object. */
    template <typename T> class TArdaRHIRef;

    /** Forward declaration of resource. */
    class IArdaRHIResource;
    /** Forward declaration of device. */
    class IArdaRHIDevice;
    /** Forward declaration of texture. */
    class IArdaRHITexture;
    /** Forward declaration of texture reference. */
    class IArdaRHITextureReference;
    /** Forward declaration of buffer. */
    class IArdaRHIBuffer;
    /** Forward declaration of uniform buffer. */
    class IArdaRHIUniformBuffer;
    /** Forward declaration of heap. */
    class IArdaRHIHeap;
    /** Forward declaration of staging texture. */
    class IArdaRHIStagingTexture;
    /** Forward declaration of event query. */
    class IArdaRHIEventQuery;
    /** Forward declaration of timer query. */
    class IArdaRHITimerQuery;
    /** Forward declaration of GPU fence. */
    class IArdaRHIGpuFence;
    /** Forward declaration of shader resource view. */
    class IArdaRHIShaderResourceView;
    /** Forward declaration of unordered access view. */
    class IArdaRHIUnorderedAccessView;
    /** Forward declaration of sampler. */
    class IArdaRHISampler;
    /** Forward declaration of shader. */
    class IArdaRHIShader;
    /** Forward declaration of shader library. */
    class IArdaRHIShaderLibrary;
    /** Forward declaration of input layout. */
    class IArdaRHIInputLayout;
    /** Forward declaration of binding layout. */
    class IArdaRHIBindingLayout;
    /** Forward declaration of binding set. */
    class IArdaRHIBindingSet;
    /** Forward declaration of descriptor table. */
    class IArdaRHIDescriptorTable;
    class IArdaRHIResourceCollection;
    /** Forward declaration of framebuffer. */
    class IArdaRHIFramebuffer;
    /** Forward declaration of graphics pipeline. */
    class IArdaRHIGraphicsPipeline;
    /** Forward declaration of compute pipeline. */
    class IArdaRHIComputePipeline;
    /** Forward declaration of meshlet pipeline. */
    class IArdaRHIMeshletPipeline;
    /** Forward declaration of accel struct. */
    class IArdaRHIAccelStruct;
    /** Forward declaration of ray tracing pipeline. */
    class IArdaRHIRayTracingPipeline;
    /** Forward declaration of shader table. */
    class IArdaRHIShaderTable;
    class IArdaRHIWorkGraphPipeline;
    class IArdaRHIShaderBundle;
    /** Forward declaration of sampler feedback texture. */
    class IArdaRHISamplerFeedbackTexture;
    /** Forward declaration of opacity micromap. */
    class IArdaRHIOpacityMicromap;
    /** Forward declaration of raster state. */
    class IArdaRHIRasterState;
    /** Forward declaration of blend state. */
    class IArdaRHIBlendState;
    /** Forward declaration of depth stencil state. */
    class IArdaRHIDepthStencilState;
    /** Forward declaration of command list. */
    class IArdaRHICommandList;

    /** Intrusive owning reference to device. */
    using FArdaRHIResourceRef = TArdaRHIRef<IArdaRHIResource>;
    /** Intrusive owning reference to device. */
    using FArdaRHIDeviceRef = TArdaRHIRef<IArdaRHIDevice>;
    /** Intrusive owning reference to texture. */
    using FArdaRHITextureRef = TArdaRHIRef<IArdaRHITexture>;
    /** Intrusive owning reference to texture reference. */
    using FArdaRHITextureReferenceRef = TArdaRHIRef<IArdaRHITextureReference>;
    /** Intrusive owning reference to buffer. */
    using FArdaRHIBufferRef = TArdaRHIRef<IArdaRHIBuffer>;
    /** Intrusive owning reference to uniform buffer. */
    using FArdaRHIUniformBufferRef = TArdaRHIRef<IArdaRHIUniformBuffer>;
    /** Intrusive owning reference to heap. */
    using FArdaRHIHeapRef = TArdaRHIRef<IArdaRHIHeap>;
    /** Intrusive owning reference to staging texture. */
    using FArdaRHIStagingTextureRef = TArdaRHIRef<IArdaRHIStagingTexture>;
    /** Intrusive owning reference to event query. */
    using FArdaRHIEventQueryRef = TArdaRHIRef<IArdaRHIEventQuery>;
    /** Intrusive owning reference to timer query. */
    using FArdaRHITimerQueryRef = TArdaRHIRef<IArdaRHITimerQuery>;
    /** Intrusive owning reference to GPU fence. */
    using FArdaRHIGpuFenceRef = TArdaRHIRef<IArdaRHIGpuFence>;
    /** Intrusive owning reference to shader resource view. */
    using FArdaRHIShaderResourceViewRef = TArdaRHIRef<IArdaRHIShaderResourceView>;
    /** Intrusive owning reference to unordered access view. */
    using FArdaRHIUnorderedAccessViewRef = TArdaRHIRef<IArdaRHIUnorderedAccessView>;
    /** Intrusive owning reference to sampler. */
    using FArdaRHISamplerRef = TArdaRHIRef<IArdaRHISampler>;
    /** Intrusive owning reference to shader. */
    using FArdaRHIShaderRef = TArdaRHIRef<IArdaRHIShader>;
    /** Intrusive owning reference to shader library. */
    using FArdaRHIShaderLibraryRef = TArdaRHIRef<IArdaRHIShaderLibrary>;
    /** Intrusive owning reference to input layout. */
    using FArdaRHIInputLayoutRef = TArdaRHIRef<IArdaRHIInputLayout>;
    /** Intrusive owning reference to binding layout. */
    using FArdaRHIBindingLayoutRef = TArdaRHIRef<IArdaRHIBindingLayout>;
    /** Intrusive owning reference to binding set. */
    using FArdaRHIBindingSetRef = TArdaRHIRef<IArdaRHIBindingSet>;
    /** Intrusive owning reference to descriptor table. */
    using FArdaRHIDescriptorTableRef = TArdaRHIRef<IArdaRHIDescriptorTable>;
    /** Intrusive owning reference to a general resource collection. */
    using FArdaRHIResourceCollectionRef = TArdaRHIRef<IArdaRHIResourceCollection>;
    /** Intrusive owning reference to framebuffer. */
    using FArdaRHIFramebufferRef = TArdaRHIRef<IArdaRHIFramebuffer>;
    /** Intrusive owning reference to graphics pipeline. */
    using FArdaRHIGraphicsPipelineRef = TArdaRHIRef<IArdaRHIGraphicsPipeline>;
    /** Intrusive owning reference to compute pipeline. */
    using FArdaRHIComputePipelineRef = TArdaRHIRef<IArdaRHIComputePipeline>;
    /** Intrusive owning reference to meshlet pipeline. */
    using FArdaRHIMeshletPipelineRef = TArdaRHIRef<IArdaRHIMeshletPipeline>;
    /** Intrusive owning reference to accel struct. */
    using FArdaRHIAccelStructRef = TArdaRHIRef<IArdaRHIAccelStruct>;
    /** Intrusive owning reference to ray tracing pipeline. */
    using FArdaRHIRayTracingPipelineRef = TArdaRHIRef<IArdaRHIRayTracingPipeline>;
    /** Intrusive owning reference to shader table. */
    using FArdaRHIShaderTableRef = TArdaRHIRef<IArdaRHIShaderTable>;
    /** Intrusive owning reference to a work-graph pipeline. */
    using FArdaRHIWorkGraphPipelineRef = TArdaRHIRef<IArdaRHIWorkGraphPipeline>;
    /** Intrusive owning reference to a shader bundle. */
    using FArdaRHIShaderBundleRef = TArdaRHIRef<IArdaRHIShaderBundle>;
    /** Intrusive owning reference to sampler feedback texture. */
    using FArdaRHISamplerFeedbackTextureRef = TArdaRHIRef<IArdaRHISamplerFeedbackTexture>;
    /** Intrusive owning reference to opacity micromap. */
    using FArdaRHIOpacityMicromapRef = TArdaRHIRef<IArdaRHIOpacityMicromap>;
    /** Intrusive owning reference to raster state. */
    using FArdaRHIRasterStateRef = TArdaRHIRef<IArdaRHIRasterState>;
    /** Intrusive owning reference to blend state. */
    using FArdaRHIBlendStateRef = TArdaRHIRef<IArdaRHIBlendState>;
    /** Intrusive owning reference to depth stencil state. */
    using FArdaRHIDepthStencilStateRef = TArdaRHIRef<IArdaRHIDepthStencilState>;
    /** Intrusive owning reference to command list. */
    using FArdaRHICommandListRef = TArdaRHIRef<IArdaRHICommandList>;
}
