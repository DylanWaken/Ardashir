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

        [[nodiscard]] const std::string& GetError() const { return m_error; }

    private:
        static std::vector<uint8_t> LoadBinary(const std::filesystem::path& path);

        nvrhi::DeviceHandle m_device;
        nvrhi::ShaderHandle m_vertexShader;
        nvrhi::ShaderHandle m_pixelShader;
        nvrhi::InputLayoutHandle m_inputLayout;
        nvrhi::GraphicsPipelineHandle m_pipeline;
        nvrhi::BufferHandle m_vertexBuffer;
        nvrhi::BufferHandle m_indexBuffer;
        nvrhi::CommandListHandle m_commandList;
        std::string m_error;
    };
}
