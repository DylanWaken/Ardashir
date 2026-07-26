#pragma once

#include "ArdaBackend.h"

#include <filesystem>
#include <string>

namespace arda::tests::nvrhi_test
{
    class FArdaTriangleRenderer
    {
    public:
        bool Initialize(
            nvrhi::DeviceHandle device,
            nvrhi::Format swapChainFormat,
            backend::EArdaBackendType backendType,
            const std::filesystem::path& shaderDirectory);
        bool RenderFrame(backend::IArdaSwapChain& swapChain);

        [[nodiscard]] const std::string& GetError() const { return mError; }

    private:
        static std::vector<uint8_t> LoadBinary(const std::filesystem::path& path);

        nvrhi::DeviceHandle mDevice;
        nvrhi::ShaderHandle mVertexShader;
        nvrhi::ShaderHandle mPixelShader;
        nvrhi::InputLayoutHandle mInputLayout;
        nvrhi::GraphicsPipelineHandle mPipeline;
        nvrhi::BufferHandle mVertexBuffer;
        nvrhi::BufferHandle mIndexBuffer;
        nvrhi::CommandListHandle mCommandList;
        std::string mError;
    };
}
