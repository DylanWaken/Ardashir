#pragma once

#include "Backend.h"

#include <array>
#include <string>

namespace arda::tests::nvrhi_test
{
    class D3D12Backend final : public Backend
    {
    public:
        ~D3D12Backend() override;

        InitializeResult Initialize(
            GLFWwindow* window,
            uint32_t width,
            uint32_t height,
            nvrhi::IMessageCallback* messageCallback) override;
        bool Resize(uint32_t width, uint32_t height) override;

        bool AcquireFrame(nvrhi::FramebufferHandle& framebuffer) override;
        void PrepareSubmit() override;
        bool Present() override;
        void WaitForIdle() override;

        [[nodiscard]] nvrhi::DeviceHandle GetDevice() const override { return m_device; }
        [[nodiscard]] nvrhi::Format GetSwapChainFormat() const override { return nvrhi::Format::BGRA8_UNORM; }
        [[nodiscard]] BackendKind GetKind() const override { return BackendKind::D3D12; }
        [[nodiscard]] uint32_t GetWidth() const override { return m_width; }
        [[nodiscard]] uint32_t GetHeight() const override { return m_height; }
        [[nodiscard]] const std::string& GetError() const override { return m_error; }

    private:
        static constexpr uint32_t BufferCount = 2;

        bool CreateSwapChainResources();
        void ReleaseSwapChainResources();

        HWND m_window = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        std::string m_error;

        Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
        Microsoft::WRL::ComPtr<ID3D12Device> m_d3dDevice;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;

        nvrhi::DeviceHandle m_nativeDevice;
        nvrhi::DeviceHandle m_device;
        std::array<nvrhi::TextureHandle, BufferCount> m_textures;
        std::array<nvrhi::FramebufferHandle, BufferCount> m_framebuffers;
    };
}
