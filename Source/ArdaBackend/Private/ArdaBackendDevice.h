/** @file ArdaBackendDevice.h
 *  Declares private backend-device abstractions and NVRHI diagnostic forwarding.
 */
#pragma once

#include "ArdaBackend.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>

namespace arda::backend
{
    /** Forwards NVRHI diagnostic messages to an Arda diagnostic callback. */
    class FArdaNvrhiMessageCallback final : public nvrhi::IMessageCallback
    {
    public:
        /** Sets the callback that receives translated NVRHI diagnostics.
         *  @param Target Callback to receive diagnostics, or null to disable forwarding.
         */
        void SetTarget(IArdaDiagnosticCallback* Target) noexcept { mTarget = Target; }

        /** Translates and forwards an NVRHI diagnostic message.
         *  @param Severity NVRHI severity assigned to the message.
         *  @param Text Null-terminated diagnostic message text.
         */
        void message(nvrhi::MessageSeverity Severity, const char* Text) override
        {
            if (!mTarget) return;
            EArdaDiagnosticSeverity ArdaSeverity = EArdaDiagnosticSeverity::Info;
            switch (Severity)
            {
            case nvrhi::MessageSeverity::Warning: ArdaSeverity = EArdaDiagnosticSeverity::Warning; break;
            case nvrhi::MessageSeverity::Error: ArdaSeverity = EArdaDiagnosticSeverity::Error; break;
            case nvrhi::MessageSeverity::Fatal: ArdaSeverity = EArdaDiagnosticSeverity::Fatal; break;
            default: break;
            }
            mTarget->Message(ArdaSeverity, Text);
        }
    private:
        /** Non-owning destination for translated diagnostic messages. */
        IArdaDiagnosticCallback* mTarget = nullptr;
    };

    /** Defines the private interface implemented by graphics backend devices. */
    class IArdaBackendDevice
    {
    public:
        /** Destroys the backend device interface. */
        virtual ~IArdaBackendDevice() = default;

        /** Initializes the backend device.
         *  @param Configuration Backend configuration used to create the device.
         *  @param WindowSurface Optional window surface used by the backend.
         *  @return Result describing whether initialization succeeded.
         */
        [[nodiscard]] virtual EArdaInitializeResult Initialize(
            const FArdaBackendConfiguration& Configuration,
            IArdaWindowSurface* WindowSurface) = 0;

        /** Creates a swap chain for the initialized backend.
         *  @param Width Initial swap-chain width in pixels.
         *  @param Height Initial swap-chain height in pixels.
         *  @return The created swap chain, or an empty pointer on failure.
         */
        [[nodiscard]] virtual eastl::unique_ptr<IArdaSwapChain> CreateSwapChain(
            uint32_t Width,
            uint32_t Height) = 0;

        /** Blocks until all queued device work has completed. */
        virtual void WaitForIdle() noexcept = 0;

        /** Gets the RHI facade for the native backend device.
         *  @return Reference to the backend's RHI device.
         */
        [[nodiscard]] virtual rhi::FArdaRHIDeviceRef GetDevice() const noexcept = 0;

        /** Gets the queues supported by the backend device.
         *  @return Queue capability flags for the device.
         */
        [[nodiscard]] virtual FArdaQueueCapabilities GetQueueCapabilities() const noexcept = 0;

        /** Gets the most recent backend error description.
         *  @return Backend-owned error text.
         */
        [[nodiscard]] virtual const eastl::string& GetError() const noexcept = 0;
    };

    /** Creates the private Direct3D 12 backend implementation.
     *  @return A newly allocated backend device.
     */
    [[nodiscard]] eastl::unique_ptr<IArdaBackendDevice> CreateD3D12BackendDevice();

    /** Creates the private Vulkan backend implementation.
     *  @return A newly allocated backend device.
     */
    [[nodiscard]] eastl::unique_ptr<IArdaBackendDevice> CreateVulkanBackendDevice();
}
