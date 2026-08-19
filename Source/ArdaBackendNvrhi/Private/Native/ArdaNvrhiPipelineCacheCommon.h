/** @file ArdaNvrhiPipelineCacheCommon.h
 *  Shared private persistence helpers for native NVRHI sidecars.
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

namespace arda::rhi::private_impl::pipeline_cache
{
    constexpr uint64_t MaxPayloadSize = 256ull * 1024ull * 1024ull;

    void Warn(
        backend::IArdaDiagnosticCallback* Callback,
        const char* Message) noexcept;

    [[nodiscard]] std::filesystem::path MakePath(
        const std::filesystem::path& Directory,
        const eastl::string& BackendName);

    [[nodiscard]] bool ReadBlob(
        const std::filesystem::path& Path,
        const eastl::string& BackendName,
        std::vector<uint8_t>& Payload);

    [[nodiscard]] bool WriteBlob(
        const std::filesystem::path& Path,
        const eastl::string& BackendName,
        const std::vector<uint8_t>& Payload);
}
