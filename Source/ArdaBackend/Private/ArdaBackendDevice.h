#pragma once

#include "ArdaBackend.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>

namespace arda::backend
{
    class IArdaBackendDevice
    {
    public:
        virtual ~IArdaBackendDevice() = default;

        [[nodiscard]] virtual EArdaInitializeResult Initialize(
            const FArdaBackendConfiguration& Configuration,
            IArdaWindowSurface* WindowSurface) = 0;
        [[nodiscard]] virtual eastl::unique_ptr<IArdaSwapChain> CreateSwapChain(
            uint32_t Width,
            uint32_t Height) = 0;
        virtual void WaitForIdle() noexcept = 0;
        [[nodiscard]] virtual nvrhi::DeviceHandle GetDevice() const noexcept = 0;
        [[nodiscard]] virtual FArdaQueueCapabilities GetQueueCapabilities() const noexcept = 0;
        [[nodiscard]] virtual const eastl::string& GetError() const noexcept = 0;
    };

    [[nodiscard]] eastl::unique_ptr<IArdaBackendDevice> CreateD3D12BackendDevice();
    [[nodiscard]] eastl::unique_ptr<IArdaBackendDevice> CreateVulkanBackendDevice();
}
