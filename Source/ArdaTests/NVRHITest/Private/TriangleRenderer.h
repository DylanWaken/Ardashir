#pragma once

#include "Backend.h"

#include <filesystem>
#include <string>

namespace arda::tests::nvrhi_test
{
    class TriangleRenderer
    {
    public:
        bool Initialize(
            nvrhi::DeviceHandle device,
            nvrhi::Format swapChainFormat,
            BackendKind backendKind,
            const std::filesystem::path& shaderDirectory);
        bool RenderFrame(Backend& backend);

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
