#include "PipelineStateCache/ArdaPipelineStateInitializer.h"

namespace arda
{
    namespace
    {
        void AddShaderLayouts(
            eastl::vector<arda::FArdaRHIBindingLayoutRef>& Destination,
            const FArdaGlobalShaderInstance* Shader)
        {
            if (Shader == nullptr)
                return;
            for (const auto& Layout : Shader->GetBindingLayouts())
            {
                bool bExists = false;
                for (const auto& Existing : Destination)
                {
                    if (Existing == Layout)
                    {
                        bExists = true;
                        break;
                    }
                }
                if (!bExists)
                    Destination.push_back(Layout);
            }
        }
    }

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
        const arda::FArdaRHIInputLayoutRef& InputLayout,
        const arda::FArdaRHIGraphicsPipelineDesc& FixedState)
    {
        return FromGlobalShaders(
            VertexShader, nullptr, nullptr, nullptr, PixelShader,
            InputLayout, FixedState);
    }

    FArdaGraphicsPipelineStateInitializer
    FArdaGraphicsPipelineStateInitializer::FromGlobalShaders(
        const FArdaGlobalShaderInstance& VertexShader,
        const FArdaGlobalShaderInstance* HullShader,
        const FArdaGlobalShaderInstance* DomainShader,
        const FArdaGlobalShaderInstance* GeometryShader,
        const FArdaGlobalShaderInstance* PixelShader,
        const arda::FArdaRHIInputLayoutRef& InputLayout,
        const arda::FArdaRHIGraphicsPipelineDesc& FixedState)
    {
        FArdaGraphicsPipelineStateInitializer Result;
        Result.mDesc = FixedState;
        Result.mDesc.mInputLayout = InputLayout;
        Result.mDesc.mVertexShader = VertexShader.GetShader();
        Result.mDesc.mHullShader = HullShader != nullptr
            ? HullShader->GetShader() : arda::FArdaRHIShaderRef{};
        Result.mDesc.mDomainShader = DomainShader != nullptr
            ? DomainShader->GetShader() : arda::FArdaRHIShaderRef{};
        Result.mDesc.mGeometryShader = GeometryShader != nullptr
            ? GeometryShader->GetShader() : arda::FArdaRHIShaderRef{};
        Result.mDesc.mPixelShader = PixelShader != nullptr
            ? PixelShader->GetShader() : arda::FArdaRHIShaderRef{};
        Result.mDesc.mBindingLayouts = VertexShader.GetBindingLayouts();
        AddShaderLayouts(Result.mDesc.mBindingLayouts, HullShader);
        AddShaderLayouts(Result.mDesc.mBindingLayouts, DomainShader);
        AddShaderLayouts(Result.mDesc.mBindingLayouts, GeometryShader);
        AddShaderLayouts(Result.mDesc.mBindingLayouts, PixelShader);
        return Result;
    }

    FArdaMeshletPipelineStateInitializer
    FArdaMeshletPipelineStateInitializer::FromGlobalShaders(
        const FArdaGlobalShaderInstance& MeshShader,
        const FArdaGlobalShaderInstance* AmplificationShader,
        const FArdaGlobalShaderInstance* PixelShader,
        const arda::FArdaRHIMeshletPipelineDesc& FixedState)
    {
        FArdaMeshletPipelineStateInitializer Result;
        Result.mDesc = FixedState;
        Result.mDesc.mAmplificationShader = AmplificationShader != nullptr
            ? AmplificationShader->GetShader() : arda::FArdaRHIShaderRef{};
        Result.mDesc.mMeshShader = MeshShader.GetShader();
        Result.mDesc.mPixelShader = PixelShader != nullptr
            ? PixelShader->GetShader() : arda::FArdaRHIShaderRef{};
        Result.mDesc.mBindingLayouts = MeshShader.GetBindingLayouts();
        AddShaderLayouts(Result.mDesc.mBindingLayouts, AmplificationShader);
        AddShaderLayouts(Result.mDesc.mBindingLayouts, PixelShader);
        return Result;
    }
}
