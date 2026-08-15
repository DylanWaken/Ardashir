/** @file ArdaShaderDirectoriesPrivate.h
 *  Declares backend lifecycle operations for the shader source directory registry.
 */
#pragma once

#include "ShaderStructs/ArdaShaderDirectories.h"

namespace arda::backend::private_api
{
    /** Marks the shader directory registry as in use by backend initialization.
     *  @return Success status, or the reason the registry cannot be acquired.
     */
    [[nodiscard]] FArdaShaderDirectoryStatus
    BeginShaderDirectoryRegistryUse();

    /** Scans registered shader directories and freezes the registry for backend use.
     *  @return Success status, or details of the scan or registry failure.
     */
    [[nodiscard]] FArdaShaderDirectoryStatus
    ScanAndFreezeShaderSourceDirectoriesForBackend();

    /** Completes registry use after a backend initialization attempt.
     *  @param BackendInitialized Whether backend initialization succeeded.
     */
    void CompleteShaderDirectoryRegistryUse(bool BackendInitialized) noexcept;

    /** Clears and unfreezes the shader directory registry after backend shutdown. */
    void ReleaseShaderDirectoryRegistryAfterShutdown() noexcept;
}
