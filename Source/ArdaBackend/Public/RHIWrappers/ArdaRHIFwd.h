#pragma once

namespace arda::rhi
{
    template <typename T> class TArdaRHIRef;

    class IArdaRHIResource;
    class IArdaRHIDevice;
    class IArdaRHITexture;
    class IArdaRHITextureReference;
    class IArdaRHIBuffer;
    class IArdaRHIUniformBuffer;
    class IArdaRHIHeap;
    class IArdaRHIStagingTexture;
    class IArdaRHIEventQuery;
    class IArdaRHITimerQuery;
    class IArdaRHIGpuFence;
    class IArdaRHIShaderResourceView;
    class IArdaRHIUnorderedAccessView;
    class IArdaRHISampler;
    class IArdaRHIShader;
    class IArdaRHIShaderLibrary;
    class IArdaRHIInputLayout;
    class IArdaRHIBindingLayout;
    class IArdaRHIBindingSet;
    class IArdaRHIDescriptorTable;
    class IArdaRHIFramebuffer;
    class IArdaRHIGraphicsPipeline;
    class IArdaRHIComputePipeline;
    class IArdaRHIMeshletPipeline;
    class IArdaRHIAccelStruct;
    class IArdaRHIRayTracingPipeline;
    class IArdaRHIShaderTable;
    class IArdaRHISamplerFeedbackTexture;
    class IArdaRHIOpacityMicromap;
    class IArdaRHIRasterState;
    class IArdaRHIBlendState;
    class IArdaRHIDepthStencilState;
    class IArdaRHICommandList;

    using FArdaRHIDeviceRef = TArdaRHIRef<IArdaRHIDevice>;
    using FArdaRHITextureRef = TArdaRHIRef<IArdaRHITexture>;
    using FArdaRHITextureReferenceRef = TArdaRHIRef<IArdaRHITextureReference>;
    using FArdaRHIBufferRef = TArdaRHIRef<IArdaRHIBuffer>;
    using FArdaRHIUniformBufferRef = TArdaRHIRef<IArdaRHIUniformBuffer>;
    using FArdaRHIHeapRef = TArdaRHIRef<IArdaRHIHeap>;
    using FArdaRHIStagingTextureRef = TArdaRHIRef<IArdaRHIStagingTexture>;
    using FArdaRHIEventQueryRef = TArdaRHIRef<IArdaRHIEventQuery>;
    using FArdaRHITimerQueryRef = TArdaRHIRef<IArdaRHITimerQuery>;
    using FArdaRHIGpuFenceRef = TArdaRHIRef<IArdaRHIGpuFence>;
    using FArdaRHIShaderResourceViewRef = TArdaRHIRef<IArdaRHIShaderResourceView>;
    using FArdaRHIUnorderedAccessViewRef = TArdaRHIRef<IArdaRHIUnorderedAccessView>;
    using FArdaRHISamplerRef = TArdaRHIRef<IArdaRHISampler>;
    using FArdaRHIShaderRef = TArdaRHIRef<IArdaRHIShader>;
    using FArdaRHIShaderLibraryRef = TArdaRHIRef<IArdaRHIShaderLibrary>;
    using FArdaRHIInputLayoutRef = TArdaRHIRef<IArdaRHIInputLayout>;
    using FArdaRHIBindingLayoutRef = TArdaRHIRef<IArdaRHIBindingLayout>;
    using FArdaRHIBindingSetRef = TArdaRHIRef<IArdaRHIBindingSet>;
    using FArdaRHIDescriptorTableRef = TArdaRHIRef<IArdaRHIDescriptorTable>;
    using FArdaRHIFramebufferRef = TArdaRHIRef<IArdaRHIFramebuffer>;
    using FArdaRHIGraphicsPipelineRef = TArdaRHIRef<IArdaRHIGraphicsPipeline>;
    using FArdaRHIComputePipelineRef = TArdaRHIRef<IArdaRHIComputePipeline>;
    using FArdaRHIMeshletPipelineRef = TArdaRHIRef<IArdaRHIMeshletPipeline>;
    using FArdaRHIAccelStructRef = TArdaRHIRef<IArdaRHIAccelStruct>;
    using FArdaRHIRayTracingPipelineRef = TArdaRHIRef<IArdaRHIRayTracingPipeline>;
    using FArdaRHIShaderTableRef = TArdaRHIRef<IArdaRHIShaderTable>;
    using FArdaRHISamplerFeedbackTextureRef = TArdaRHIRef<IArdaRHISamplerFeedbackTexture>;
    using FArdaRHIOpacityMicromapRef = TArdaRHIRef<IArdaRHIOpacityMicromap>;
    using FArdaRHIRasterStateRef = TArdaRHIRef<IArdaRHIRasterState>;
    using FArdaRHIBlendStateRef = TArdaRHIRef<IArdaRHIBlendState>;
    using FArdaRHIDepthStencilStateRef = TArdaRHIRef<IArdaRHIDepthStencilState>;
    using FArdaRHICommandListRef = TArdaRHIRef<IArdaRHICommandList>;
}
