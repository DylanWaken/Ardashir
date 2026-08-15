#pragma once

#include "RHIWrappers/ArdaRHIResources.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

namespace arda::backend
{
    /** Renderer-facing compute PSO description; never a concrete RHI pipeline. */
    struct FArdaComputePipelineStateInitializer
    {
        rhi::FArdaRHIComputePipelineDesc mDesc;

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
        FArdaGraphicsPipelineStateInitializer() { mDesc.mSampleCount = 0; }

        rhi::FArdaRHIGraphicsPipelineDesc mDesc;

        [[nodiscard]] static FArdaGraphicsPipelineStateInitializer FromGlobalShaders(
            const FArdaGlobalShaderInstance& VertexShader,
            const FArdaGlobalShaderInstance* PixelShader,
            const rhi::FArdaRHIInputLayoutRef& InputLayout,
            const rhi::FArdaRHIGraphicsPipelineDesc& FixedState = {});
    };
}
