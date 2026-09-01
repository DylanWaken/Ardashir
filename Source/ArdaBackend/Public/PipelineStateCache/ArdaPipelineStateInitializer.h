/** @file ArdaPipelineStateInitializer.h
 *  @brief Declares renderer-facing compute, graphics, and meshlet pipeline initializers.
 */
#pragma once

#include "RHI/ArdaRHIResources.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

namespace arda::backend
{
    /** Renderer-facing compute PSO description; never a concrete RHI pipeline. */
    struct FArdaComputePipelineStateInitializer
    {
        /** RHI compute pipeline description to resolve. */
        rhi::FArdaRHIComputePipelineDesc mDesc;

        /**
         * Creates a compute initializer from a global shader.
         * @param Shader Compute global shader instance.
         * @param DebugName Optional pipeline debug name.
         * @return A compute pipeline initializer referencing the shader.
         */
        [[nodiscard]] static FArdaComputePipelineStateInitializer FromGlobalShader(
            const FArdaGlobalShaderInstance& Shader,
            const char* DebugName = nullptr);
    };

    /**
     * Renderer-facing graphics PSO description.
     *
     * Empty color formats, Unknown depth format, and a zero sample count are
     * completed from the framebuffer supplied when the PSO is resolved.
     */
    struct FArdaGraphicsPipelineStateInitializer
    {
        /** Creates an initializer with framebuffer-derived sample count. */
        FArdaGraphicsPipelineStateInitializer() { mDesc.mSampleCount = 0; }

        /** RHI graphics pipeline description to complete and resolve. */
        rhi::FArdaRHIGraphicsPipelineDesc mDesc;

        /**
         * Creates a graphics initializer from global shaders and fixed state.
         * @param VertexShader Vertex global shader instance.
         * @param PixelShader Optional pixel global shader instance.
         * @param InputLayout Vertex input layout.
         * @param FixedState Fixed-function graphics state.
         * @return A graphics pipeline initializer referencing the supplied shaders.
         */
        [[nodiscard]] static FArdaGraphicsPipelineStateInitializer FromGlobalShaders(
            const FArdaGlobalShaderInstance& VertexShader,
            const FArdaGlobalShaderInstance* PixelShader,
            const rhi::FArdaRHIInputLayoutRef& InputLayout,
            const rhi::FArdaRHIGraphicsPipelineDesc& FixedState = {});

        /**
         * Creates a graphics initializer with optional tessellation and geometry stages.
         * @param VertexShader Vertex global shader instance.
         * @param HullShader Optional hull global shader instance.
         * @param DomainShader Optional domain global shader instance.
         * @param GeometryShader Optional geometry global shader instance.
         * @param PixelShader Optional pixel global shader instance.
         * @param InputLayout Vertex input layout.
         * @param FixedState Fixed-function graphics state.
         * @return A graphics pipeline initializer referencing the supplied shaders.
         */
        [[nodiscard]] static FArdaGraphicsPipelineStateInitializer FromGlobalShaders(
            const FArdaGlobalShaderInstance& VertexShader,
            const FArdaGlobalShaderInstance* HullShader,
            const FArdaGlobalShaderInstance* DomainShader,
            const FArdaGlobalShaderInstance* GeometryShader,
            const FArdaGlobalShaderInstance* PixelShader,
            const rhi::FArdaRHIInputLayoutRef& InputLayout,
            const rhi::FArdaRHIGraphicsPipelineDesc& FixedState = {});
    };

    /**
     * Renderer-facing meshlet PSO description.
     *
     * Empty color formats, Unknown depth format, and a zero sample count are
     * completed from the framebuffer supplied when the PSO is resolved.
     */
    struct FArdaMeshletPipelineStateInitializer
    {
        /** Creates an initializer with framebuffer-derived sample count. */
        FArdaMeshletPipelineStateInitializer() { mDesc.mSampleCount = 0; }

        /** RHI meshlet pipeline description to complete and resolve. */
        rhi::FArdaRHIMeshletPipelineDesc mDesc;

        /**
         * Creates a meshlet initializer from global shaders and fixed state.
         * @param MeshShader Mesh global shader instance.
         * @param AmplificationShader Optional amplification global shader instance.
         * @param PixelShader Optional pixel global shader instance.
         * @param FixedState Fixed-function meshlet state.
         * @return A meshlet pipeline initializer referencing the supplied shaders.
         */
        [[nodiscard]] static FArdaMeshletPipelineStateInitializer FromGlobalShaders(
            const FArdaGlobalShaderInstance& MeshShader,
            const FArdaGlobalShaderInstance* AmplificationShader,
            const FArdaGlobalShaderInstance* PixelShader,
            const rhi::FArdaRHIMeshletPipelineDesc& FixedState = {});
    };
}
