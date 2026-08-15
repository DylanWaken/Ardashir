#pragma once

#include "ArdaBackend.h"
#include "ArdaRenderGraph.h"

#include <filesystem>
#include <EASTL/string.h>

namespace arda::tests::rhi_test
{
    class FArdaTriangleRenderer
    {
    public:
        bool Initialize(
            const backend::FArdaDeviceContext& deviceContext,
            rhi::EArdaRHIFormat swapChainFormat,
            const std::filesystem::path& shaderDirectory);
        bool RenderFrame(backend::IArdaSwapChain& swapChain);

        [[nodiscard]] const eastl::string& GetError() const { return mError; }

    private:
        static bool LoadBinary(
            const std::filesystem::path& path,
            eastl::vector<uint8_t>& binary,
            eastl::string& error);
        [[nodiscard]] render_graph::FARDGRenderGraphContext CreateGraphContext() const;

        rhi::FArdaRHIDeviceRef mDevice;
        render_graph::FARDGQueueCapabilities mQueueCapabilities;
        rhi::FArdaRHIShaderRef mVertexShader;
        rhi::FArdaRHIShaderRef mPixelShader;
        rhi::FArdaRHIInputLayoutRef mInputLayout;
        rhi::FArdaRHIGraphicsPipelineRef mPipeline;
        rhi::FArdaRHIBufferRef mVertexBuffer;
        rhi::FArdaRHIBufferRef mIndexBuffer;
        eastl::string mError;
    };
}
