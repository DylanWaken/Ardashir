#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace arda::tests::nvrhi_test
{
    enum class BackendKind
    {
        D3D12,
        Vulkan
    };

    enum class InitializeResult
    {
        Success,
        Unavailable,
        Failure
    };

    class Backend
    {
    public:
        virtual ~Backend() = default;

        virtual InitializeResult Initialize(
            GLFWwindow* window,
            uint32_t width,
            uint32_t height,
            nvrhi::IMessageCallback* messageCallback) = 0;
        virtual bool Resize(uint32_t width, uint32_t height) = 0;

        virtual bool AcquireFrame(nvrhi::FramebufferHandle& framebuffer) = 0;
        virtual void PrepareSubmit() = 0;
        virtual bool Present() = 0;
        virtual void WaitForIdle() = 0;

        [[nodiscard]] virtual nvrhi::DeviceHandle GetDevice() const = 0;
        [[nodiscard]] virtual nvrhi::Format GetSwapChainFormat() const = 0;
        [[nodiscard]] virtual BackendKind GetKind() const = 0;
        [[nodiscard]] virtual uint32_t GetWidth() const = 0;
        [[nodiscard]] virtual uint32_t GetHeight() const = 0;
        [[nodiscard]] virtual const std::string& GetError() const = 0;
    };
}
