#pragma once

#include "Backend.h"

#include <string>
#include <vector>

namespace arda::tests::nvrhi_test
{
    class VulkanBackend final : public Backend
    {
    public:
        ~VulkanBackend() override;

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
        [[nodiscard]] BackendKind GetKind() const override { return BackendKind::Vulkan; }
        [[nodiscard]] uint32_t GetWidth() const override { return m_width; }
        [[nodiscard]] uint32_t GetHeight() const override { return m_height; }
        [[nodiscard]] const std::string& GetError() const override { return m_error; }

    private:
        static constexpr uint32_t FramesInFlight = 2;

        bool CreateSwapChain(uint32_t width, uint32_t height);
        void ReleaseSwapChain();
        bool CreateFrameSyncObjects();

        GLFWwindow* m_window = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_queueFamily = 0;
        uint32_t m_imageIndex = 0;
        uint32_t m_frameIndex = 0;
        std::string m_error;

        vk::detail::DynamicLoader m_loader;
        vk::Instance m_instance;
        vk::SurfaceKHR m_surface;
        vk::PhysicalDevice m_physicalDevice;
        vk::Device m_vkDevice;
        vk::Queue m_queue;
        vk::SwapchainKHR m_swapChain;

        nvrhi::vulkan::DeviceHandle m_nativeDevice;
        nvrhi::DeviceHandle m_device;
        std::vector<nvrhi::TextureHandle> m_textures;
        std::vector<nvrhi::FramebufferHandle> m_framebuffers;
        std::array<vk::Semaphore, FramesInFlight> m_imageAvailable;
        std::array<vk::Semaphore, FramesInFlight> m_renderFinished;
    };
}
