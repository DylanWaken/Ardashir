#include "ShaderStructs/ArdaShaderDirectories.h"

#include "ArdaString.h"
#include "ShaderStructs/ArdaShaderDirectoriesPrivate.h"

#include <EASTL/sort.h>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <unordered_map>

namespace arda::backend
{
    namespace
    {
        struct FDirectoryRegistry
        {
            std::mutex mMutex;
            eastl::vector<FArdaShaderSourceDirectory> mDirectories;
            eastl::vector<FArdaShaderSourceFile> mFiles;
            FArdaShaderDirectoryStatus mLastStatus;
            bool mbFrozen = false;
            bool mbRegistryInUse = false;
            bool mbBackendInitialized = false;
        };

        FDirectoryRegistry& GetRegistry()
        {
            static FDirectoryRegistry Registry;
            return Registry;
        }

        std::string PortableKey(std::string Value)
        {
            std::transform(
                Value.begin(),
                Value.end(),
                Value.begin(),
                [](unsigned char Character)
                {
                    return static_cast<char>(std::tolower(Character));
                });
            return Value;
        }

        bool IsPortableVirtualCharacter(unsigned char Character)
        {
            return (Character >= 'A' && Character <= 'Z') ||
                (Character >= 'a' && Character <= 'z') ||
                (Character >= '0' && Character <= '9') ||
                Character == '_' || Character == '-' || Character == '.';
        }

        FArdaShaderDirectoryStatus MakeStatus(
            EArdaShaderDirectoryError Code,
            const eastl::string& Message,
            const eastl::string& VirtualPath = {})
        {
            FArdaShaderDirectoryStatus Status;
            Status.mCode = Code;
            Status.mMessage = Message;
            Status.mVirtualPath = VirtualPath;
            return Status;
        }

        FArdaShaderDirectoryStatus& Publish(
            FDirectoryRegistry& Registry,
            FArdaShaderDirectoryStatus Status)
        {
            Registry.mLastStatus = eastl::move(Status);
            return Registry.mLastStatus;
        }

        bool HasAcceptedExtension(const std::string& VirtualPath)
        {
            const std::string Extension =
                PortableKey(std::filesystem::path(VirtualPath).extension().string());
            return Extension == ".hlsl" || Extension == ".hlsli" ||
                Extension == ".usf" || Extension == ".ush";
        }

        FArdaShaderDirectoryStatus ValidateVirtualPath(
            const eastl::string& Path,
            bool IsRoot)
        {
            const std::string Value = ToStd(Path);
            if (Value.empty() || Value.front() != '/' ||
                Value.find('\\') != std::string::npos ||
                Value.find("//") != std::string::npos ||
                (!IsRoot && Value == "/") ||
                (Value.size() > 1 && Value.back() == '/'))
            {
                return MakeStatus(
                    EArdaShaderDirectoryError::InvalidVirtualPath,
                    eastl::string("Virtual shader path must be normalized, absolute, and use forward slashes: ") +
                        Path,
                    Path);
            }

            size_t Start = 1;
            while (Start <= Value.size())
            {
                const size_t End = Value.find('/', Start);
                const std::string Component = Value.substr(Start, End - Start);
                if (Component == "." || Component == ".." || Component.empty())
                {
                    if (!(Value == "/" && Component.empty()))
                    {
                        return MakeStatus(
                            EArdaShaderDirectoryError::InvalidVirtualPath,
                            eastl::string("Virtual shader path contains an invalid component: ") +
                                Path,
                            Path);
                    }
                }
                for (const unsigned char Character : Component)
                {
                    if (!IsPortableVirtualCharacter(Character))
                    {
                        return MakeStatus(
                            EArdaShaderDirectoryError::InvalidVirtualPath,
                            eastl::string("Virtual shader path contains a non-portable character; use ASCII letters, digits, '_', '-', or '.': ") +
                                Path,
                            Path);
                    }
                }
                if (End == std::string::npos)
                    break;
                Start = End + 1;
            }

            if (!IsRoot && !HasAcceptedExtension(Value))
            {
                return MakeStatus(
                    EArdaShaderDirectoryError::UnsupportedExtension,
                    eastl::string("Virtual shader source has an unsupported extension: ") +
                        Path,
                    Path);
            }
            return {};
        }

        bool PhysicalComponentEqual(
            const std::filesystem::path& Left,
            const std::filesystem::path& Right)
        {
#if defined(_WIN32)
            return PortableKey(Left.generic_string()) ==
                PortableKey(Right.generic_string());
#else
            return Left == Right;
#endif
        }

        bool IsPathContainedBy(
            const std::filesystem::path& Candidate,
            const std::filesystem::path& Root,
            bool AllowEqual)
        {
            auto CandidateIt = Candidate.begin();
            auto RootIt = Root.begin();
            for (; RootIt != Root.end(); ++RootIt, ++CandidateIt)
            {
                if (CandidateIt == Candidate.end() ||
                    !PhysicalComponentEqual(*CandidateIt, *RootIt))
                {
                    return false;
                }
            }
            return AllowEqual || CandidateIt != Candidate.end();
        }

        bool IsUnderVirtualRoot(
            const std::string& Path,
            const std::string& Root);

        bool IsProperlyNestedRegistration(
            const FArdaShaderSourceDirectory& Parent,
            const FArdaShaderSourceDirectory& Child)
        {
            if (!Parent.mbExclusiveMapping || !Child.mbExclusiveMapping)
                return false;
            const std::string ParentRoot =
                PortableKey(ToStd(Parent.mVirtualRoot));
            const std::string ChildRoot =
                PortableKey(ToStd(Child.mVirtualRoot));
            if (!IsPathContainedBy(
                    Child.mPhysicalDirectory,
                    Parent.mPhysicalDirectory,
                    false) ||
                !IsUnderVirtualRoot(ChildRoot, ParentRoot))
            {
                return false;
            }
            const std::string VirtualRelative = ParentRoot == "/"
                ? ChildRoot.substr(1)
                : ChildRoot.substr(ParentRoot.size() + 1);
            const std::string PhysicalRelative = PortableKey(
                Child.mPhysicalDirectory
                    .lexically_relative(Parent.mPhysicalDirectory)
                    .generic_string());
            return PhysicalRelative == VirtualRelative;
        }

        FArdaShaderDirectoryStatus RegistryMutationStatus(
            FDirectoryRegistry& Registry)
        {
            if (Registry.mbRegistryInUse)
            {
                return MakeStatus(
                    EArdaShaderDirectoryError::RegistryInUse,
                    "Shader source directory registry is in use by backend initialization.");
            }
            if (Registry.mbBackendInitialized)
            {
                return MakeStatus(
                    EArdaShaderDirectoryError::BackendInitialized,
                    "Shader source directories cannot be changed while the backend is initialized.");
            }
            return {};
        }

        FArdaShaderDirectoryStatus CanonicalizeDirectory(
            const std::filesystem::path& Input,
            std::filesystem::path& Output)
        {
            std::error_code Error;
            std::filesystem::path Absolute = std::filesystem::absolute(Input, Error);
            if (Error)
            {
                return MakeStatus(
                    EArdaShaderDirectoryError::InvalidPhysicalDirectory,
                    "Unable to make shader source directory absolute.");
            }
            Output = std::filesystem::canonical(Absolute, Error);
            if (Error || !std::filesystem::is_directory(Output, Error) || Error)
            {
                return MakeStatus(
                    EArdaShaderDirectoryError::InvalidPhysicalDirectory,
                    ToEastl(
                        "Shader source directory does not exist or is not a directory: " +
                        Absolute.string()));
            }
            return {};
        }

        bool IsUnderVirtualRoot(
            const std::string& Path,
            const std::string& Root)
        {
            return Root == "/" ||
                (Path.size() > Root.size() &&
                 Path.compare(0, Root.size(), Root) == 0 &&
                 Path[Root.size()] == '/');
        }

        bool IsShadowedByNestedExclusiveRoot(
            const FArdaShaderSourceDirectory& Directory,
            const eastl::vector<FArdaShaderSourceDirectory>& Directories,
            const std::string& VirtualPath)
        {
            if (!Directory.mbExclusiveMapping)
                return false;
            const std::string CurrentRoot =
                PortableKey(ToStd(Directory.mVirtualRoot));
            const std::string PathKey = PortableKey(VirtualPath);
            for (const auto& Other : Directories)
            {
                if (!Other.mbExclusiveMapping ||
                    Other.mVirtualRoot.size() <= Directory.mVirtualRoot.size())
                {
                    continue;
                }
                const std::string OtherRoot =
                    PortableKey(ToStd(Other.mVirtualRoot));
                if (IsUnderVirtualRoot(OtherRoot, CurrentRoot) &&
                    IsUnderVirtualRoot(PathKey, OtherRoot))
                {
                    return true;
                }
            }
            return false;
        }

        eastl::string MakeVirtualPath(
            const eastl::string& Root,
            const std::filesystem::path& Relative)
        {
            const std::string Suffix = Relative.generic_string();
            if (Root == "/")
                return ToEastl("/" + Suffix);
            return Root + "/" + ToEastl(Suffix);
        }

        FArdaShaderDirectoryStatus AddDirectory(
            const std::filesystem::path& RealDirectory,
            const eastl::string& VirtualRoot,
            bool Exclusive)
        {
            FDirectoryRegistry& Registry = GetRegistry();
            std::lock_guard<std::mutex> Lock(Registry.mMutex);
            FArdaShaderDirectoryStatus MutationStatus =
                RegistryMutationStatus(Registry);
            if (!MutationStatus)
                return Publish(Registry, eastl::move(MutationStatus));
            if (Registry.mbFrozen)
            {
                return Publish(
                    Registry,
                    MakeStatus(
                        EArdaShaderDirectoryError::Frozen,
                        "Shader source directories are frozen until backend shutdown."));
            }

            FArdaShaderDirectoryStatus VirtualStatus =
                ValidateVirtualPath(VirtualRoot, true);
            if (!VirtualStatus)
                return Publish(Registry, eastl::move(VirtualStatus));

            std::filesystem::path CanonicalDirectory;
            FArdaShaderDirectoryStatus PhysicalStatus =
                CanonicalizeDirectory(RealDirectory, CanonicalDirectory);
            if (!PhysicalStatus)
                return Publish(Registry, eastl::move(PhysicalStatus));

            const std::string RootKey = PortableKey(ToStd(VirtualRoot));
            const FArdaShaderSourceDirectory Candidate{
                VirtualRoot, CanonicalDirectory, Exclusive
            };
            for (const auto& Existing : Registry.mDirectories)
            {
                const bool SameRoot =
                    PortableKey(ToStd(Existing.mVirtualRoot)) == RootKey;
                const bool SamePhysical = IsPathContainedBy(
                    CanonicalDirectory,
                    Existing.mPhysicalDirectory,
                    true) && IsPathContainedBy(
                    Existing.mPhysicalDirectory,
                    CanonicalDirectory,
                    true);
                if (SameRoot && SamePhysical &&
                    Existing.mbExclusiveMapping == Exclusive)
                {
                    return Publish(Registry, {});
                }
                if (Exclusive && Existing.mbExclusiveMapping && SameRoot)
                {
                    auto Status = MakeStatus(
                        EArdaShaderDirectoryError::DuplicateMappingRoot,
                        eastl::string("Duplicate exclusive shader mapping root: ") +
                            VirtualRoot,
                        VirtualRoot);
                    Status.mPhysicalDirectories.push_back(
                        Existing.mPhysicalDirectory);
                    Status.mPhysicalDirectories.push_back(CanonicalDirectory);
                    return Publish(Registry, eastl::move(Status));
                }

                const bool PhysicalOverlap =
                    IsPathContainedBy(
                        CanonicalDirectory,
                        Existing.mPhysicalDirectory,
                        true) ||
                    IsPathContainedBy(
                        Existing.mPhysicalDirectory,
                        CanonicalDirectory,
                        true);
                if (PhysicalOverlap &&
                    !IsProperlyNestedRegistration(Existing, Candidate) &&
                    !IsProperlyNestedRegistration(Candidate, Existing))
                {
                    auto Status = MakeStatus(
                        EArdaShaderDirectoryError::AmbiguousPhysicalFile,
                        eastl::string("Overlapping physical shader directories are mounted incompatibly at '") +
                            Existing.mVirtualRoot + "' and '" + VirtualRoot +
                            "'. Only matching nested exclusive physical and virtual roots may overlap.",
                        VirtualRoot);
                    Status.mPhysicalDirectories.push_back(
                        Existing.mPhysicalDirectory);
                    Status.mPhysicalDirectories.push_back(CanonicalDirectory);
                    return Publish(Registry, eastl::move(Status));
                }
            }

            Registry.mDirectories.push_back(Candidate);
            return Publish(Registry, {});
        }
    }

    FArdaShaderDirectoryStatus AddShaderSourceDirectory(
        const std::filesystem::path& RealDirectory,
        const eastl::string& VirtualRoot)
    {
        return AddDirectory(RealDirectory, VirtualRoot, false);
    }

    FArdaShaderDirectoryStatus AddShaderSourceDirectoryMapping(
        const eastl::string& VirtualRoot,
        const std::filesystem::path& RealDirectory)
    {
        return AddDirectory(RealDirectory, VirtualRoot, true);
    }

    namespace
    {
    FArdaShaderDirectoryStatus ScanAndFreezeImpl(bool BackendAccess)
    {
        FDirectoryRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        if (!BackendAccess)
        {
            FArdaShaderDirectoryStatus MutationStatus =
                RegistryMutationStatus(Registry);
            if (!MutationStatus)
                return Publish(Registry, eastl::move(MutationStatus));
        }
        if (Registry.mbFrozen)
            return Publish(Registry, {});

        eastl::vector<FArdaShaderSourceFile> Candidates;
        for (const auto& Directory : Registry.mDirectories)
        {
            std::error_code Error;
            std::filesystem::recursive_directory_iterator Iterator(
                Directory.mPhysicalDirectory,
                std::filesystem::directory_options::none,
                Error);
            const std::filesystem::recursive_directory_iterator End;
            if (Error)
            {
                return Publish(
                    Registry,
                    MakeStatus(
                        EArdaShaderDirectoryError::ScanFailed,
                        ToEastl(
                            "Unable to scan shader source directory: " +
                            Directory.mPhysicalDirectory.string())));
            }

            while (Iterator != End)
            {
                const std::filesystem::directory_entry Entry = *Iterator;
                const auto SymlinkStatus = Entry.symlink_status(Error);
                if (Error)
                {
                    return Publish(
                        Registry,
                        MakeStatus(
                            EArdaShaderDirectoryError::ScanFailed,
                            ToEastl(
                                "Unable to inspect shader source path: " +
                                Entry.path().string())));
                }
                const bool IsSymlink =
                    std::filesystem::is_symlink(SymlinkStatus);
                const bool IsDirectory = Entry.is_directory(Error);
                if (Error)
                {
                    return Publish(
                        Registry,
                        MakeStatus(
                            EArdaShaderDirectoryError::ScanFailed,
                            ToEastl(
                                "Unable to inspect shader source path: " +
                                Entry.path().string())));
                }
                if (IsSymlink && IsDirectory)
                {
                    Iterator.disable_recursion_pending();
                }
                else if (Entry.is_regular_file(Error))
                {
                    if (Error)
                    {
                        return Publish(
                            Registry,
                            MakeStatus(
                                EArdaShaderDirectoryError::ScanFailed,
                                ToEastl(
                                    "Unable to inspect shader source file: " +
                                    Entry.path().string())));
                    }
                    const std::filesystem::path Relative =
                        Entry.path().lexically_relative(
                            Directory.mPhysicalDirectory);
                    if (Relative.empty())
                    {
                        return Publish(
                            Registry,
                            MakeStatus(
                                EArdaShaderDirectoryError::ScanFailed,
                                ToEastl(
                                    "Unable to relativize shader source file: " +
                                    Entry.path().string())));
                    }
                    if (HasAcceptedExtension(Relative.generic_string()))
                    {
                        const eastl::string VirtualPath =
                            MakeVirtualPath(Directory.mVirtualRoot, Relative);
                        FArdaShaderDirectoryStatus Validation =
                            ValidateVirtualPath(VirtualPath, false);
                        if (!Validation)
                            return Publish(Registry, eastl::move(Validation));
                        if (IsShadowedByNestedExclusiveRoot(
                                Directory,
                                Registry.mDirectories,
                                ToStd(VirtualPath)))
                        {
                            Iterator.increment(Error);
                            if (Error)
                            {
                                return Publish(
                                    Registry,
                                    MakeStatus(
                                        EArdaShaderDirectoryError::ScanFailed,
                                        ToEastl(
                                            "Unable to continue scanning shader source directory: " +
                                            Directory.mPhysicalDirectory.string())));
                            }
                            continue;
                        }
                        const std::filesystem::path CanonicalFile =
                            std::filesystem::canonical(Entry.path(), Error);
                        if (Error)
                        {
                            return Publish(
                                Registry,
                                MakeStatus(
                                    EArdaShaderDirectoryError::ScanFailed,
                                    ToEastl(
                                        "Unable to canonicalize shader source file: " +
                                        Entry.path().string()),
                                    VirtualPath));
                        }
                        if (!IsPathContainedBy(
                                CanonicalFile,
                                Directory.mPhysicalDirectory,
                                false))
                        {
                            auto Status = MakeStatus(
                                EArdaShaderDirectoryError::EscapesPhysicalDirectory,
                                ToEastl(
                                    "Shader source symlink escapes its registered physical directory: " +
                                    Entry.path().string() + " -> " +
                                    CanonicalFile.string()),
                                VirtualPath);
                            Status.mPhysicalPaths.push_back(Entry.path());
                            Status.mPhysicalPaths.push_back(CanonicalFile);
                            Status.mPhysicalDirectories.push_back(
                                Directory.mPhysicalDirectory);
                            return Publish(Registry, eastl::move(Status));
                        }
                        Candidates.push_back(
                            { VirtualPath,
                              CanonicalFile,
                              Directory.mPhysicalDirectory });
                    }
                }
                else if (Error)
                {
                    return Publish(
                        Registry,
                        MakeStatus(
                            EArdaShaderDirectoryError::ScanFailed,
                            ToEastl(
                                "Unable to inspect shader source path: " +
                                Entry.path().string())));
                }

                Iterator.increment(Error);
                if (Error)
                {
                    return Publish(
                        Registry,
                        MakeStatus(
                            EArdaShaderDirectoryError::ScanFailed,
                            ToEastl(
                                "Unable to continue scanning shader source directory: " +
                                Directory.mPhysicalDirectory.string())));
                }
            }
        }

        eastl::sort(
            Candidates.begin(),
            Candidates.end(),
            [](const auto& Left, const auto& Right)
            {
                const std::string LeftKey = PortableKey(ToStd(Left.mVirtualPath));
                const std::string RightKey = PortableKey(ToStd(Right.mVirtualPath));
                if (LeftKey != RightKey)
                    return LeftKey < RightKey;
                return Left.mPhysicalPath.generic_string() <
                    Right.mPhysicalPath.generic_string();
            });

        std::unordered_map<std::string, size_t> VirtualFiles;
        std::unordered_map<std::string, size_t> PhysicalFiles;
        for (size_t Index = 0; Index < Candidates.size(); ++Index)
        {
            const auto& Candidate = Candidates[Index];
            const std::string VirtualKey =
                PortableKey(ToStd(Candidate.mVirtualPath));
            const auto VirtualExisting = VirtualFiles.find(VirtualKey);
            if (VirtualExisting != VirtualFiles.end())
            {
                const auto& Existing = Candidates[VirtualExisting->second];
                auto Status = MakeStatus(
                    EArdaShaderDirectoryError::DuplicateVirtualPath,
                    eastl::string("Duplicate virtual shader source '") +
                        Candidate.mVirtualPath + "' from '" +
                        ToEastl(Existing.mPhysicalPath.string()) + "' and '" +
                        ToEastl(Candidate.mPhysicalPath.string()) + "'.",
                    Candidate.mVirtualPath);
                Status.mPhysicalPaths.push_back(Existing.mPhysicalPath);
                Status.mPhysicalPaths.push_back(Candidate.mPhysicalPath);
                Status.mPhysicalDirectories.push_back(
                    Existing.mPhysicalDirectory);
                Status.mPhysicalDirectories.push_back(
                    Candidate.mPhysicalDirectory);
                return Publish(Registry, eastl::move(Status));
            }
            VirtualFiles.emplace(VirtualKey, Index);

            const std::string PhysicalKey =
                PortableKey(Candidate.mPhysicalPath.generic_string());
            const auto PhysicalExisting = PhysicalFiles.find(PhysicalKey);
            if (PhysicalExisting != PhysicalFiles.end() &&
                PortableKey(ToStd(
                    Candidates[PhysicalExisting->second].mVirtualPath)) !=
                    VirtualKey)
            {
                const auto& Existing = Candidates[PhysicalExisting->second];
                auto Status = MakeStatus(
                    EArdaShaderDirectoryError::AmbiguousPhysicalFile,
                    eastl::string("Physical shader source is mounted at both '") +
                        Existing.mVirtualPath + "' and '" +
                        Candidate.mVirtualPath + "': " +
                        ToEastl(Candidate.mPhysicalPath.string()),
                    Candidate.mVirtualPath);
                Status.mPhysicalPaths.push_back(Candidate.mPhysicalPath);
                return Publish(Registry, eastl::move(Status));
            }
            PhysicalFiles.emplace(PhysicalKey, Index);
        }

        Registry.mFiles = eastl::move(Candidates);
        Registry.mbFrozen = true;
        return Publish(Registry, {});
    }
    }

    FArdaShaderDirectoryStatus ScanAndFreezeShaderSourceDirectories()
    {
        return ScanAndFreezeImpl(false);
    }

    eastl::vector<FArdaShaderSourceDirectory>
    EnumerateShaderSourceDirectories()
    {
        FDirectoryRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        auto Result = Registry.mDirectories;
        eastl::sort(
            Result.begin(),
            Result.end(),
            [](const auto& Left, const auto& Right)
            {
                if (Left.mVirtualRoot != Right.mVirtualRoot)
                    return Left.mVirtualRoot < Right.mVirtualRoot;
                return Left.mPhysicalDirectory.generic_string() <
                    Right.mPhysicalDirectory.generic_string();
            });
        return Result;
    }

    eastl::vector<FArdaShaderSourceFile> EnumerateShaderSourceFiles()
    {
        FDirectoryRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        return Registry.mFiles;
    }

    FArdaShaderDirectoryStatus ResolveVirtualShaderSource(
        const eastl::string& VirtualPath,
        std::filesystem::path& OutPhysicalPath)
    {
        OutPhysicalPath.clear();
        FDirectoryRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        const FArdaShaderDirectoryStatus Validation =
            ValidateVirtualPath(VirtualPath, false);
        if (!Validation)
            return Publish(Registry, Validation);
        if (!Registry.mbFrozen)
        {
            return Publish(
                Registry,
                MakeStatus(
                    EArdaShaderDirectoryError::NotFrozen,
                    "Shader source manifest has not been scanned and frozen.",
                    VirtualPath));
        }

        const std::string Key = PortableKey(ToStd(VirtualPath));
        for (const auto& File : Registry.mFiles)
        {
            if (PortableKey(ToStd(File.mVirtualPath)) == Key)
            {
                OutPhysicalPath = File.mPhysicalPath;
                return Publish(Registry, {});
            }
        }
        return Publish(
            Registry,
            MakeStatus(
                EArdaShaderDirectoryError::MissingVirtualSource,
                eastl::string("Virtual shader source is not present in the frozen manifest: ") +
                    VirtualPath,
                VirtualPath));
    }

    FArdaShaderDirectoryState GetShaderSourceDirectoryState()
    {
        FDirectoryRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        return {
            Registry.mbFrozen,
            Registry.mbRegistryInUse,
            Registry.mbBackendInitialized,
            Registry.mDirectories,
            Registry.mFiles,
            Registry.mLastStatus
        };
    }

    FArdaShaderDirectoryStatus GetShaderSourceDirectoryStatus()
    {
        FDirectoryRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        return Registry.mLastStatus;
    }

    bool AreShaderSourceDirectoriesFrozen() noexcept
    {
        FDirectoryRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        return Registry.mbFrozen;
    }

    FArdaShaderDirectoryStatus UnfreezeShaderSourceDirectories()
    {
        FDirectoryRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        FArdaShaderDirectoryStatus MutationStatus =
            RegistryMutationStatus(Registry);
        if (!MutationStatus)
            return Publish(Registry, eastl::move(MutationStatus));
        Registry.mFiles.clear();
        Registry.mbFrozen = false;
        return Publish(Registry, {});
    }

    FArdaShaderDirectoryStatus ClearShaderSourceDirectories()
    {
        FDirectoryRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        FArdaShaderDirectoryStatus MutationStatus =
            RegistryMutationStatus(Registry);
        if (!MutationStatus)
            return Publish(Registry, eastl::move(MutationStatus));
        if (Registry.mbFrozen)
        {
            return Publish(
                Registry,
                MakeStatus(
                    EArdaShaderDirectoryError::Frozen,
                    "Unfreeze shader source directories before clearing registrations."));
        }
        Registry.mDirectories.clear();
        Registry.mFiles.clear();
        Registry.mbFrozen = false;
        Registry.mLastStatus = {};
        return {};
    }

    namespace private_api
    {
        FArdaShaderDirectoryStatus BeginShaderDirectoryRegistryUse()
        {
            FDirectoryRegistry& Registry = GetRegistry();
            std::lock_guard<std::mutex> Lock(Registry.mMutex);
            FArdaShaderDirectoryStatus MutationStatus =
                RegistryMutationStatus(Registry);
            if (!MutationStatus)
                return Publish(Registry, eastl::move(MutationStatus));
            Registry.mbRegistryInUse = true;
            return Publish(Registry, {});
        }

        FArdaShaderDirectoryStatus
        ScanAndFreezeShaderSourceDirectoriesForBackend()
        {
            FDirectoryRegistry& Registry = GetRegistry();
            {
                std::lock_guard<std::mutex> Lock(Registry.mMutex);
                if (!Registry.mbRegistryInUse)
                {
                    return Publish(
                        Registry,
                        MakeStatus(
                            EArdaShaderDirectoryError::RegistryInUse,
                            "Backend shader scan requires an active registry-use guard."));
                }
            }
            return ScanAndFreezeImpl(true);
        }

        void CompleteShaderDirectoryRegistryUse(
            bool BackendInitialized) noexcept
        {
            FDirectoryRegistry& Registry = GetRegistry();
            std::lock_guard<std::mutex> Lock(Registry.mMutex);
            Registry.mbBackendInitialized = BackendInitialized;
            Registry.mbRegistryInUse = false;
            if (!BackendInitialized)
            {
                Registry.mFiles.clear();
                Registry.mbFrozen = false;
            }
        }

        void ReleaseShaderDirectoryRegistryAfterShutdown() noexcept
        {
            FDirectoryRegistry& Registry = GetRegistry();
            std::lock_guard<std::mutex> Lock(Registry.mMutex);
            Registry.mbBackendInitialized = false;
            Registry.mbRegistryInUse = false;
            Registry.mFiles.clear();
            Registry.mbFrozen = false;
        }
    }
}
