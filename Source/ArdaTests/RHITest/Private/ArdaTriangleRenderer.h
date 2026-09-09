#pragma once

#include "ArdaSwapChain.h"
#include "ArdaRenderGraph.h"

#include <filesystem>
#include <EASTL/string.h>

namespace arda
{
    class FArdaTriangleRenderer
    {
    public:
        bool Initialize(
            arda::FArdaRHIDeviceRef device,
            arda::EArdaRHIFormat swapChainFormat,
            const std::filesystem::path& shaderDirectory);
        bool RenderFrame(arda::IArdaSwapChain& swapChain);

        [[nodiscard]] const eastl::string& GetError() const { return mError; }

    private:
        static bool LoadBinary(
            const std::filesystem::path& path,
            eastl::vector<uint8_t>& binary,
            eastl::string& error);
        [[nodiscard]] arda::FARDGRenderGraphContext CreateGraphContext() const;

        arda::FArdaRHIDeviceRef mDevice;
        arda::FArdaRHIShaderRef mVertexShader;
        arda::FArdaRHIShaderRef mPixelShader;
        arda::FArdaRHIInputLayoutRef mInputLayout;
        arda::FArdaRHIGraphicsPipelineRef mPipeline;
        arda::FArdaRHIBufferRef mVertexBuffer;
        arda::FArdaRHIBufferRef mIndexBuffer;
        eastl::string mError;
    };
}
