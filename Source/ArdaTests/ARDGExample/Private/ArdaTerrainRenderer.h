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
            nvrhi::Format swapChainFormat,
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
        static bool LoadBinary(
            const std::filesystem::path& path,
            eastl::vector<uint8_t>& binary,
            eastl::string& error);
        bool CreateShadersAndPipelines(
            nvrhi::Format swapChainFormat,
            const std::filesystem::path& shaderDirectory,
            bool vulkan);
        bool CreateSettingsUploadBuffer();
        bool CreateCameraResources();

        [[nodiscard]] render_graph::FARDGRenderGraphContext
        CreateGraphContext() const;

        nvrhi::DeviceHandle mDevice;
        render_graph::FARDGQueueCapabilities mQueueCapabilities;

        nvrhi::ShaderHandle mGenerateShader;
        nvrhi::ShaderHandle mErodeShader;
        nvrhi::ShaderHandle mTriangulateShader;
        nvrhi::ShaderHandle mTerrainVertexShader;
        nvrhi::ShaderHandle mTerrainPixelShader;
        nvrhi::ShaderHandle mOverlayVertexShader;
        nvrhi::ShaderHandle mOverlayPixelShader;

        nvrhi::BindingLayoutHandle mGenerateBindingLayout;
        nvrhi::BindingLayoutHandle mErodeBindingLayout;
        nvrhi::BindingLayoutHandle mTriangulateBindingLayout;
        nvrhi::BindingLayoutHandle mCameraBindingLayout;
        nvrhi::BindingLayoutHandle mTerrainPixelBindingLayout;
        nvrhi::ComputePipelineHandle mGeneratePipeline;
        nvrhi::ComputePipelineHandle mErodePipeline;
        nvrhi::ComputePipelineHandle mTriangulatePipeline;
        nvrhi::InputLayoutHandle mTerrainInputLayout;
        nvrhi::GraphicsPipelineHandle mTerrainPipeline;
        nvrhi::GraphicsPipelineHandle mOverlayPipeline;
        nvrhi::BufferHandle mSettingsUploadBuffer;
        nvrhi::BufferHandle mCameraBuffer;
        nvrhi::BindingSetHandle mCameraBindingSet;

        float mCameraPosition[3] = {-0.96875f, -0.96875f, 0.8125f};
        float mCameraYaw = 0.78539816f;
        float mCameraPitch = -0.67453292f;
        float mElapsedSeconds = 0.0f;

        eastl::string mError;
    };
}
