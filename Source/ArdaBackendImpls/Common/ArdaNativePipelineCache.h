/** @file ArdaNativePipelineCache.h
 * Shared, provider-owned persistence helpers for native pipeline caches.
 */
#pragma once

#include "ArdaDevice.h"

#include <EASTL/string.h>

#include <filesystem>
#include <vector>

namespace arda::backend
{
    class IArdaDiagnosticCallback;
}

namespace arda::rhi::native::pipeline_cache
{
    inline constexpr uint64_t MaxPayloadSize = 256ull * 1024ull * 1024ull;

    void Message(
        backend::IArdaDiagnosticCallback* Callback,
        backend::EArdaDiagnosticSeverity Severity,
        const char* Text) noexcept;

    [[nodiscard]] std::filesystem::path MakePath(
        const std::filesystem::path& Directory,
        const eastl::string& BackendName);

    [[nodiscard]] bool ReadBlob(
        const std::filesystem::path& Path,
        const eastl::string& BackendName,
        backend::EArdaBackendType Backend,
        std::vector<uint8_t>& Payload);

    [[nodiscard]] bool WriteBlob(
        const std::filesystem::path& Path,
        const eastl::string& BackendName,
        backend::EArdaBackendType Backend,
        const std::vector<uint8_t>& Payload);
}
