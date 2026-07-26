#pragma once

#include "ArdaBackend.h"

#include <memory>
#include <string>

namespace arda::backend
{
    class IArdaBackendDevice
    {
    public:
        virtual ~IArdaBackendDevice() = default;

        [[nodiscard]] virtual EArdaInitializeResult Initialize(
            const FArdaBackendConfiguration& Configuration,
            IArdaWindowSurface* WindowSurface) = 0;
        [[nodiscard]] virtual std::unique_ptr<IArdaSwapChain> CreateSwapChain(
            uint32_t Width,
            uint32_t Height) = 0;
        virtual void WaitForIdle() noexcept = 0;
        [[nodiscard]] virtual nvrhi::DeviceHandle GetDevice() const noexcept = 0;
        [[nodiscard]] virtual const std::string& GetError() const noexcept = 0;
    };

    [[nodiscard]] std::unique_ptr<IArdaBackendDevice> CreateD3D12BackendDevice();
    [[nodiscard]] std::unique_ptr<IArdaBackendDevice> CreateVulkanBackendDevice();
}
