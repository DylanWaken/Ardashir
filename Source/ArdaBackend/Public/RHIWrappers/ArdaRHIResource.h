/** @file ArdaRHIResource.h
 * Declares the common intrusive-lifetime interface and resource type identifiers.
 */

#pragma once

#include "ArdaRHIFwd.h"

#include <cstdint>

namespace arda::rhi
{
    /** Enumerates resource type values. */
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
        CommandList,
        Count
    };

    /** Interface for resource. */
    class IArdaRHIResource
    {
    public:
        /** Performs the add operation. */
        virtual void AddRef() noexcept = 0;
        /** Performs the release operation. */
        virtual void Release() noexcept = 0;
        /**
         * Returns the resource type.
         * @return The requested value.
         */
        [[nodiscard]] virtual EArdaRHIResourceType GetResourceType() const noexcept = 0;
        /**
         * Returns the debug name.
         * @return The requested object pointer.
         */
        [[nodiscard]] virtual const char* GetDebugName() const noexcept = 0;

    protected:
        /** Releases the retained resource object. */
        virtual ~IArdaRHIResource() = default;
    };
}
