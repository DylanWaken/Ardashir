/** @file ArdaShaderDirectories.h
 *  @brief Declares virtual shader-source directory registration and resolution.
 */
#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <filesystem>

namespace arda::backend
{
    /** Identifies shader-source directory registry failures. */
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

    /** Describes a directory-registry operation result and related paths. */
    struct FArdaShaderDirectoryStatus
    {
        /** Directory-registry result code. */
        EArdaShaderDirectoryError mCode = EArdaShaderDirectoryError::None;
        /** Human-readable diagnostic message. */
        eastl::string mMessage;
        /** Virtual shader path associated with the result. */
        eastl::string mVirtualPath;
        /** Physical files associated with the result. */
        eastl::vector<std::filesystem::path> mPhysicalPaths;
        /** Physical directories associated with the result. */
        eastl::vector<std::filesystem::path> mPhysicalDirectories;

        /** @return True when the operation succeeded. */
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mCode == EArdaShaderDirectoryError::None;
        }
    };

    /** Maps a virtual shader root to a registered physical directory. */
    struct FArdaShaderSourceDirectory
    {
        /** Normalized virtual root. */
        eastl::string mVirtualRoot;
        /** Registered physical directory. */
        std::filesystem::path mPhysicalDirectory;
        /** Whether this directory exclusively owns its virtual root. */
        bool mbExclusiveMapping = false;
    };

    /** Associates one virtual shader filename with its physical source file. */
    struct FArdaShaderSourceFile
    {
        /** Normalized full virtual shader path. */
        eastl::string mVirtualPath;
        /** Physical shader source path. */
        std::filesystem::path mPhysicalPath;
        /** Registered directory containing the physical source. */
        std::filesystem::path mPhysicalDirectory;
    };

    /** Snapshot of the shader-source directory registry. */
    struct FArdaShaderDirectoryState
    {
        /** Whether the source manifest has been frozen. */
        bool mbFrozen = false;
        /** Whether shader type registration currently uses the registry. */
        bool mbRegistryInUse = false;
        /** Whether the backend is initialized. */
        bool mbBackendInitialized = false;
        /** Registered source directories. */
        eastl::vector<FArdaShaderSourceDirectory> mDirectories;
        /** Files in the frozen source manifest. */
        eastl::vector<FArdaShaderSourceFile> mFiles;
        /** Result of the most recent registry operation. */
        FArdaShaderDirectoryStatus mLastStatus;
    };

    /**
     * Adds an overlay directory. Exact repeats are idempotent. Overlays may
     * share a virtual root, but every discovered full virtual filename must
     * remain globally unique, including against nested exclusive mappings.
     * @param RealDirectory Physical directory to register.
     * @param VirtualRoot Virtual root exposed by the directory.
     * @return Registration status.
     */
    [[nodiscard]] FArdaShaderDirectoryStatus AddShaderSourceDirectory(
        const std::filesystem::path& RealDirectory,
        const eastl::string& VirtualRoot = "/");

    /**
     * Adds an exclusive Unreal-style root mapping. Exact repeats are
     * idempotent; the same root mapped elsewhere is rejected. Properly nested
     * physical/virtual exclusive mappings use the deepest matching root.
     * Other overlapping physical mounts are rejected as ambiguous.
     * @param VirtualRoot Exclusive virtual root to register.
     * @param RealDirectory Physical directory mapped to the root.
     * @return Registration status.
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
     * @return Scan and freeze status.
     */
    [[nodiscard]] FArdaShaderDirectoryStatus ScanAndFreezeShaderSourceDirectories();

    /** @return All currently registered shader source directories. */
    [[nodiscard]] eastl::vector<FArdaShaderSourceDirectory>
    EnumerateShaderSourceDirectories();
    /** @return All files in the frozen shader source manifest. */
    [[nodiscard]] eastl::vector<FArdaShaderSourceFile>
    EnumerateShaderSourceFiles();
    /**
     * Resolves a virtual shader path through the frozen manifest.
     * @param VirtualPath Full virtual shader path to resolve.
     * @param OutPhysicalPath Receives the matching physical path.
     * @return Resolution status.
     */
    [[nodiscard]] FArdaShaderDirectoryStatus ResolveVirtualShaderSource(
        const eastl::string& VirtualPath,
        std::filesystem::path& OutPhysicalPath);
    /** @return A snapshot of the shader-source directory registry. */
    [[nodiscard]] FArdaShaderDirectoryState GetShaderSourceDirectoryState();
    /** @return The result of the most recent registry operation. */
    [[nodiscard]] FArdaShaderDirectoryStatus GetShaderSourceDirectoryStatus();
    /** @return True when the shader source manifest is frozen. */
    [[nodiscard]] bool AreShaderSourceDirectoriesFrozen() noexcept;

    /**
     * Drops only the frozen manifest and retains registrations for a restart.
     * Clear drops both. Mutations are rejected throughout backend
     * initialization and while the backend is live.
     * @return Unfreeze status.
     */
    [[nodiscard]] FArdaShaderDirectoryStatus UnfreezeShaderSourceDirectories();
    /** @return Status from clearing registrations and the frozen manifest. */
    [[nodiscard]] FArdaShaderDirectoryStatus ClearShaderSourceDirectories();
}
