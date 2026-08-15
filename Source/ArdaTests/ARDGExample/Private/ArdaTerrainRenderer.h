#pragma once

#include "ArdaBackend.h"
#include "ArdaRenderGraph.h"

#include <filesystem>
#include <EASTL/string.h>

namespace arda::tests::ardg_example
{
    class FArdaTerrainRenderer
    {
    public:
        bool Initialize(
            const backend::FArdaDeviceContext& deviceContext,
            rhi::EArdaRHIFormat swapChainFormat,
            const std::filesystem::path& shaderDirectory);
        void UpdateCamera(
            float forward,
            float right,
            float lookX,
            float lookY,
            float deltaSeconds);
        bool RenderFrame(backend::IArdaSwapChain& swapChain);

        [[nodiscard]] const eastl::string& GetError() const { return mError; }

    private:
        bool CreateShadersAndPipelines(
            const backend::FArdaDeviceContext& deviceContext,
            rhi::EArdaRHIFormat swapChainFormat,
            const std::filesystem::path& shaderDirectory);
        bool CreateSettingsUploadBuffer();
        bool CreateCameraResources();

        [[nodiscard]] render_graph::FARDGRenderGraphContext
        CreateGraphContext() const;

        rhi::FArdaRHIDeviceRef mDevice;
        render_graph::FARDGQueueCapabilities mQueueCapabilities;
        backend::FArdaGlobalShaderMap mShaderMap;
        const backend::FArdaGlobalShaderInstance* mGenerateShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mErodeShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mTriangulateShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mTerrainVertexShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mTerrainPixelShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mOverlayVertexShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mOverlayPixelShader = nullptr;
        rhi::FArdaRHIComputePipelineRef mGeneratePipeline;
        rhi::FArdaRHIComputePipelineRef mErodePipeline;
        rhi::FArdaRHIComputePipelineRef mTriangulatePipeline;
        rhi::FArdaRHIInputLayoutRef mTerrainInputLayout;
        rhi::FArdaRHIGraphicsPipelineRef mTerrainPipeline;
        rhi::FArdaRHIGraphicsPipelineRef mOverlayPipeline;
        rhi::FArdaRHIBufferRef mSettingsUploadBuffer;
        rhi::FArdaRHIBufferRef mCameraBuffer;
        rhi::FArdaRHIBindingSetRef mCameraBindingSet;

        float mCameraPosition[3] = {-0.96875f, -0.96875f, 0.8125f};
        float mCameraYaw = 0.78539816f;
        float mCameraPitch = -0.67453292f;
        float mElapsedSeconds = 0.0f;

        eastl::string mError;
    };
}
