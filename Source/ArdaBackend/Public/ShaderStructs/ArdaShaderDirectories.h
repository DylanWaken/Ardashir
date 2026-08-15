#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <filesystem>

namespace arda::backend
{
    enum class EArdaShaderDirectoryError : uint8_t
    {
        None,
        Frozen,
        BackendInitialized,
        RegistryInUse,
        InvalidVirtualPath,
        InvalidPhysicalDirectory,
        EscapesPhysicalDirectory,
        UnsupportedExtension,
        NotFrozen,
        DuplicateMappingRoot,
        DuplicateVirtualPath,
        AmbiguousPhysicalFile,
        ScanFailed,
        MissingVirtualSource
    };

    struct FArdaShaderDirectoryStatus
    {
        EArdaShaderDirectoryError mCode = EArdaShaderDirectoryError::None;
        eastl::string mMessage;
        eastl::string mVirtualPath;
        eastl::vector<std::filesystem::path> mPhysicalPaths;
        eastl::vector<std::filesystem::path> mPhysicalDirectories;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mCode == EArdaShaderDirectoryError::None;
        }
    };

    struct FArdaShaderSourceDirectory
    {
        eastl::string mVirtualRoot;
        std::filesystem::path mPhysicalDirectory;
        bool mbExclusiveMapping = false;
    };

    struct FArdaShaderSourceFile
    {
        eastl::string mVirtualPath;
        std::filesystem::path mPhysicalPath;
        std::filesystem::path mPhysicalDirectory;
    };

    struct FArdaShaderDirectoryState
    {
        bool mbFrozen = false;
        bool mbRegistryInUse = false;
        bool mbBackendInitialized = false;
        eastl::vector<FArdaShaderSourceDirectory> mDirectories;
        eastl::vector<FArdaShaderSourceFile> mFiles;
        FArdaShaderDirectoryStatus mLastStatus;
    };

    /**
     * Adds an overlay directory. Exact repeats are idempotent. Overlays may
     * share a virtual root, but every discovered full virtual filename must
     * remain globally unique, including against nested exclusive mappings.
     */
    [[nodiscard]] FArdaShaderDirectoryStatus AddShaderSourceDirectory(
        const std::filesystem::path& RealDirectory,
        const eastl::string& VirtualRoot = "/");

    /**
     * Adds an exclusive Unreal-style root mapping. Exact repeats are
     * idempotent; the same root mapped elsewhere is rejected. Properly nested
     * physical/virtual exclusive mappings use the deepest matching root.
     * Other overlapping physical mounts are rejected as ambiguous.
     */
    [[nodiscard]] FArdaShaderDirectoryStatus AddShaderSourceDirectoryMapping(
        const eastl::string& VirtualRoot,
        const std::filesystem::path& RealDirectory);

    /**
     * Virtual components are restricted to portable ASCII letters, digits,
     * underscore, hyphen, and period. Scanning validates every shader path,
     * then atomically freezes a deterministic immutable manifest.
     * Register all directories before backend initialization; initialization
     * calls this automatically and fails before device creation on any error.
     */
    [[nodiscard]] FArdaShaderDirectoryStatus ScanAndFreezeShaderSourceDirectories();

    [[nodiscard]] eastl::vector<FArdaShaderSourceDirectory>
    EnumerateShaderSourceDirectories();
    [[nodiscard]] eastl::vector<FArdaShaderSourceFile>
    EnumerateShaderSourceFiles();
    [[nodiscard]] FArdaShaderDirectoryStatus ResolveVirtualShaderSource(
        const eastl::string& VirtualPath,
        std::filesystem::path& OutPhysicalPath);
    [[nodiscard]] FArdaShaderDirectoryState GetShaderSourceDirectoryState();
    [[nodiscard]] FArdaShaderDirectoryStatus GetShaderSourceDirectoryStatus();
    [[nodiscard]] bool AreShaderSourceDirectoriesFrozen() noexcept;

    /**
     * Drops only the frozen manifest and retains registrations for a restart.
     * Clear drops both. Mutations are rejected throughout backend
     * initialization and while the backend is live.
     */
    [[nodiscard]] FArdaShaderDirectoryStatus UnfreezeShaderSourceDirectories();
    [[nodiscard]] FArdaShaderDirectoryStatus ClearShaderSourceDirectories();
}
