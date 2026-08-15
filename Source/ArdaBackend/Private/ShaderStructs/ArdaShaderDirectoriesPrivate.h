#pragma once

#include "ShaderStructs/ArdaShaderDirectories.h"

namespace arda::backend::private_api
{
    [[nodiscard]] FArdaShaderDirectoryStatus
    BeginShaderDirectoryRegistryUse();
    [[nodiscard]] FArdaShaderDirectoryStatus
    ScanAndFreezeShaderSourceDirectoriesForBackend();
    void CompleteShaderDirectoryRegistryUse(bool BackendInitialized) noexcept;
    void ReleaseShaderDirectoryRegistryAfterShutdown() noexcept;
}
