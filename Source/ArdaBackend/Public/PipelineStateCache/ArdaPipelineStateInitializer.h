/** @file ArdaPipelineStateInitializer.h
 *  @brief Declares renderer-facing compute and graphics pipeline initializers.
 */
#pragma once

#include "RHIWrappers/ArdaRHIResources.h"
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
    };
}
