#pragma once

#include "ArdaBackend.h"
#include "ArdaRenderGraph.h"

#include <filesystem>
#include <string>

namespace arda::tests::nvrhi_test
{
    class FArdaTriangleRenderer
    {
    public:
        bool Initialize(
            const backend::FArdaDeviceContext& deviceContext,
            nvrhi::Format swapChainFormat,
            const std::filesystem::path& shaderDirectory);
        bool RenderFrame(backend::IArdaSwapChain& swapChain);

        [[nodiscard]] const std::string& GetError() const { return mError; }

    private:
        static bool LoadBinary(
            const std::filesystem::path& path,
            std::vector<uint8_t>& binary,
            std::string& error);
        [[nodiscard]] render_graph::FARDGRenderGraphContext CreateGraphContext() const;

        nvrhi::DeviceHandle mDevice;
        render_graph::FARDGQueueCapabilities mQueueCapabilities;
        nvrhi::ShaderHandle mVertexShader;
        nvrhi::ShaderHandle mPixelShader;
        nvrhi::InputLayoutHandle mInputLayout;
        nvrhi::GraphicsPipelineHandle mPipeline;
        nvrhi::BufferHandle mVertexBuffer;
        nvrhi::BufferHandle mIndexBuffer;
        std::string mError;
    };
}
