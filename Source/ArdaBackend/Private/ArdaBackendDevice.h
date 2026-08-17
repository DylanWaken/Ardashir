/** @file ArdaBackendDevice.h
 *  Declares private backend-device abstractions and NVRHI diagnostic forwarding.
 */
#pragma once

#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>

namespace arda::backend
{
    class IArdaExternalDeviceProvider;
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

    /** Creates the private Direct3D 12 backend implementation.
     *  @return A newly allocated backend device.
     */
    [[nodiscard]] eastl::unique_ptr<IArdaBackendDevice> CreateD3D12BackendDevice();

    /** Creates the private Vulkan backend implementation.
     *  @return A newly allocated backend device.
     */
    [[nodiscard]] eastl::unique_ptr<IArdaBackendDevice> CreateVulkanBackendDevice();

    /** Creates the private backend implementation that wraps externally supplied handles.
     *  @return A newly allocated external backend device.
     */
    [[nodiscard]] eastl::unique_ptr<IArdaBackendDevice> CreateExternalBackendDevice();

    /** Replaces the process-wide backend error while holding no backend-state lock.
     *  @param Error Null-terminated diagnostic text.
     */
    void SetBackendError(const char* Error);
}
