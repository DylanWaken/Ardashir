#include "PipelineStateCache/ArdaPipelineStateInitializer.h"

namespace arda::backend
{
    FArdaComputePipelineStateInitializer
    FArdaComputePipelineStateInitializer::FromGlobalShader(
        const FArdaGlobalShaderInstance& Shader,
        const char* DebugName)
    {
        FArdaComputePipelineStateInitializer Result;
        Result.mDesc.mComputeShader = Shader.GetShader();
        Result.mDesc.mBindingLayouts = Shader.GetBindingLayouts();
        Result.mDesc.mDebugName = DebugName ? DebugName : Shader.GetType().GetName();
        return Result;
    }

    FArdaGraphicsPipelineStateInitializer
    FArdaGraphicsPipelineStateInitializer::FromGlobalShaders(
        const FArdaGlobalShaderInstance& VertexShader,
        const FArdaGlobalShaderInstance* PixelShader,
        const rhi::FArdaRHIInputLayoutRef& InputLayout,
        const rhi::FArdaRHIGraphicsPipelineDesc& FixedState)
    {
        FArdaGraphicsPipelineStateInitializer Result;
        Result.mDesc = FixedState;
        Result.mDesc.mInputLayout = InputLayout;
        Result.mDesc.mVertexShader = VertexShader.GetShader();
        Result.mDesc.mPixelShader =
            PixelShader != nullptr ? PixelShader->GetShader() : rhi::FArdaRHIShaderRef{};
        Result.mDesc.mBindingLayouts = VertexShader.GetBindingLayouts();
        if (PixelShader != nullptr)
        {
            for (const auto& Layout : PixelShader->GetBindingLayouts())
            {
                bool bExists = false;
                for (const auto& Existing : Result.mDesc.mBindingLayouts)
                {
                    if (Existing == Layout)
                    {
                        bExists = true;
                        break;
                    }
                }
                if (!bExists)
                    Result.mDesc.mBindingLayouts.push_back(Layout);
            }
        }
        return Result;
    }
}
