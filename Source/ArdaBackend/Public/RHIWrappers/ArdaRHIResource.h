#pragma once

#include "ArdaRHIFwd.h"

#include <cstdint>

namespace arda::rhi
{
    enum class EArdaRHIResourceType : uint8_t
    {
        Device,
        Texture,
        TextureReference,
        Buffer,
        UniformBuffer,
        Heap,
        StagingTexture,
        EventQuery,
        TimerQuery,
        GpuFence,
        ShaderResourceView,
        UnorderedAccessView,
        Sampler,
        Shader,
        ShaderLibrary,
        InputLayout,
        BindingLayout,
        BindingSet,
        DescriptorTable,
        Framebuffer,
        GraphicsPipeline,
        ComputePipeline,
        MeshletPipeline,
        AccelStruct,
        RayTracingPipeline,
        ShaderTable,
        SamplerFeedbackTexture,
        OpacityMicromap,
        RasterState,
        BlendState,
        DepthStencilState,
        CommandList
    };

    class IArdaRHIResource
    {
    public:
        virtual void AddRef() noexcept = 0;
        virtual void Release() noexcept = 0;
        [[nodiscard]] virtual EArdaRHIResourceType GetResourceType() const noexcept = 0;
        [[nodiscard]] virtual const char* GetDebugName() const noexcept = 0;

    protected:
        virtual ~IArdaRHIResource() = default;
    };
}
