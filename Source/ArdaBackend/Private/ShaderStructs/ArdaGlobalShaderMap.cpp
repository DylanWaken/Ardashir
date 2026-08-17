#include "ShaderStructs/ArdaGlobalShaderMap.h"

#include "ArdaBackendProvider.h"
#include "ShaderStructs/ArdaShaderCompiler.h"
#include "ShaderStructs/ArdaShaderDirectories.h"

#include <fstream>

namespace arda::backend
{
    namespace
    {
        eastl::string ToEastlString(const std::filesystem::path& Path)
        {
            const std::string Value = Path.string();
            return eastl::string(Value.data(), Value.size());
        }

        FArdaGlobalShaderMapDiagnostic MakeDiagnostic(
            EArdaGlobalShaderMapError Code,
            const char* ShaderType,
            const std::filesystem::path& Path,
            const eastl::string& Message)
        {
            FArdaGlobalShaderMapDiagnostic Result;
            Result.mCode = Code;
            Result.mShaderType = ShaderType != nullptr ? ShaderType : "";
            Result.mPath = ToEastlString(Path);
            Result.mMessage = Message;
            return Result;
        }

        void AttachSource(
            FArdaGlobalShaderMapDiagnostic& Diagnostic,
            const FArdaShaderType& Type)
        {
            const char* Source = Type.GetSourceStem();
            if (Source == nullptr || Source[0] != '/')
                return;
            Diagnostic.mVirtualSource = Source;
            std::filesystem::path PhysicalSource;
            if (ResolveVirtualShaderSource(Source, PhysicalSource))
                Diagnostic.mPhysicalSource = ToEastlString(PhysicalSource);
        }

        bool IsContainedArtifactPath(
            const std::filesystem::path& Directory,
            const std::filesystem::path& Path)
        {
            std::error_code Error;
            const auto Root = std::filesystem::absolute(Directory, Error).lexically_normal();
            if (Error)
                return false;
            const auto Candidate = std::filesystem::absolute(Path, Error).lexically_normal();
            return !Error && Candidate.parent_path() == Root &&
                Candidate.stem().string().find("..") == std::string::npos;
        }
    }

    const char* GetShaderArtifactExtension(EArdaBackendType Backend) noexcept
    {
        IArdaBackendModule* Module = FindDefaultBackendModule(Backend);
        return Module
            ? Module->GetDescriptor().mShaderArtifactExtension.c_str()
            : "";
    }

    const char* GetShaderArtifactExtension(const char* BackendName) noexcept
    {
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        return Module
            ? Module->GetDescriptor().mShaderArtifactExtension.c_str()
            : "";
    }

    FArdaShaderBytecodeResult LoadShaderBytecode(
        const std::filesystem::path& Path)
    {
        FArdaShaderBytecodeResult Result;
        std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
        if (!Stream)
        {
            Result.mDiagnostic = MakeDiagnostic(
                EArdaGlobalShaderMapError::BytecodeMissing,
                nullptr,
                Path,
                eastl::string("Unable to open shader artifact: ") +
                    ToEastlString(Path));
            return Result;
        }
        const std::streamsize Size = Stream.tellg();
        if (Size <= 0)
        {
            Result.mDiagnostic = MakeDiagnostic(
                EArdaGlobalShaderMapError::BytecodeEmpty,
                nullptr,
                Path,
                eastl::string("Shader artifact is empty: ") +
                    ToEastlString(Path));
            return Result;
        }
        Result.mBytecode.resize(static_cast<size_t>(Size));
        Stream.seekg(0);
        Stream.read(
            reinterpret_cast<char*>(Result.mBytecode.data()),
            Size);
        if (!Stream)
        {
            Result.mBytecode.clear();
            Result.mDiagnostic = MakeDiagnostic(
                EArdaGlobalShaderMapError::BytecodeReadFailed,
                nullptr,
                Path,
                eastl::string("Unable to read shader artifact: ") +
                    ToEastlString(Path));
        }
        return Result;
    }

    bool FArdaGlobalShaderMap::Initialize(
        const FArdaDeviceContext& DeviceContext,
        const std::filesystem::path& ShaderDirectory)
    {
        std::lock_guard<std::mutex> Lock(mLoadMutex);
        std::error_code DirectoryError;
        const std::filesystem::path ResolvedDirectory =
            ShaderDirectory.empty()
                ? std::filesystem::path{}
                : std::filesystem::absolute(
                    ShaderDirectory, DirectoryError).lexically_normal();
        if (DirectoryError || ResolvedDirectory.empty())
        {
            mDiagnostics.clear();
            mDiagnostics.push_back(MakeDiagnostic(
                EArdaGlobalShaderMapError::BytecodeMissing,
                nullptr,
                ShaderDirectory,
                "The global shader artifact directory must resolve to a non-empty absolute path."));
            return false;
        }
        if (mbInitialized &&
            mDevice == DeviceContext.mDevice &&
            mTarget.mBackendName == DeviceContext.mBackendName &&
            mDirectory == ResolvedDirectory)
        {
            return true;
        }
        if (mbInitialized)
        {
            mDiagnostics.push_back(MakeDiagnostic(
                EArdaGlobalShaderMapError::ResetRequired,
                nullptr,
                ResolvedDirectory,
                "Reset the initialized global shader map before changing its device, backend, or directory."));
            return false;
        }
        mDiagnostics.clear();
        if (!DeviceContext.mDevice)
        {
            mDiagnostics.push_back(MakeDiagnostic(
                EArdaGlobalShaderMapError::InvalidDevice,
                nullptr,
                {},
                "Cannot initialize the global shader map without a device."));
            return false;
        }
        const FArdaShaderRegistrationStatus Registration =
            FArdaShaderTypeRegistration::CommitAll();
        if (!Registration)
        {
            mDiagnostics.push_back(MakeDiagnostic(
                EArdaGlobalShaderMapError::RegistrationFailed,
                nullptr,
                {},
                Registration.mMessage));
            return false;
        }
        FArdaShaderTarget Target;
        const bool bResolvedTarget = DeviceContext.mBackendName.empty()
            ? ResolveDefaultShaderTarget(DeviceContext.mBackend, Target)
            : ResolveShaderTarget(DeviceContext.mBackendName.c_str(), Target);
        if (!bResolvedTarget)
        {
            mDiagnostics.push_back(MakeDiagnostic(
                EArdaGlobalShaderMapError::RegistrationFailed,
                nullptr, {}, "The device backend module has no registered shader target."));
            return false;
        }

        eastl::vector<FArdaGlobalShaderInstance> Slots;
        for (const FArdaShaderType& Type :
             FArdaShaderTypeRegistration::EnumerateSnapshots())
        {
            for (uint32_t PermutationId = 0;
                 PermutationId < Type.GetPermutationCount();
                 ++PermutationId)
            {
                if (!Type.ShouldCompilePermutation(Target, PermutationId))
                {
                    continue;
                }
                const eastl::string ArtifactStem =
                    Type.GetPermutationArtifactStem(PermutationId);
                const std::filesystem::path Path = ResolvedDirectory /
                    (std::string(ArtifactStem.data(), ArtifactStem.size()) +
                     std::string(Target.mArtifactExtension.data(),
                         Target.mArtifactExtension.size()));
                if (!IsContainedArtifactPath(ResolvedDirectory, Path))
                {
                    mDiagnostics.push_back(MakeDiagnostic(
                        EArdaGlobalShaderMapError::RegistrationFailed,
                        Type.GetName(), Path,
                        "Generated shader artifact path escapes its shader directory."));
                    return false;
                }
                FArdaGlobalShaderInstance Shader;
                Shader.mType = Type;
                Shader.mPermutationId = PermutationId;
                Slots.push_back(eastl::move(Shader));
            }
        }

        mDevice = DeviceContext.mDevice;
        mTarget = eastl::move(Target);
        mMode = GetBackendConfiguration().mShaderCompilationMode;
        mDirectory = ResolvedDirectory;
        mShaders = eastl::move(Slots);
        mLoadStates.assign(mShaders.size(), 0);
        mbInitialized = true;
        if (mMode != EArdaShaderCompilationMode::OnDemand)
        {
            for (size_t Index = 0; Index < mShaders.size(); ++Index)
            {
                if (!EnsureSlotLoadedLocked(Index))
                {
                    mShaders.clear();
                    mLoadStates.clear();
                    mDevice = nullptr;
                    mbInitialized = false;
                    return false;
                }
            }
        }
        return true;
    }

    bool FArdaGlobalShaderMap::Initialize(
        const FArdaDeviceContext& DeviceContext)
    {
        return Initialize(
            DeviceContext,
            GetBackendConfiguration().mShaderCacheDirectory);
    }

    bool FArdaGlobalShaderMap::EnsureSlotLoaded(size_t Index) const
    {
        std::lock_guard<std::mutex> Lock(mLoadMutex);
        return EnsureSlotLoadedLocked(Index);
    }

    bool FArdaGlobalShaderMap::EnsureSlotLoadedLocked(size_t Index) const
    {
        if (!mbInitialized || Index >= mShaders.size())
            return false;
        if (mLoadStates[Index] == 1)
            return true;
        if (mLoadStates[Index] == 2)
            return false;

        FArdaGlobalShaderInstance& Shader = mShaders[Index];
        const FArdaShaderType& Type = Shader.mType;
        const eastl::string ArtifactStem =
            Type.GetPermutationArtifactStem(Shader.mPermutationId);
        const std::filesystem::path Path = mDirectory /
            (std::string(ArtifactStem.data(), ArtifactStem.size()) +
             std::string(mTarget.mArtifactExtension.data(),
                 mTarget.mArtifactExtension.size()));

        if (mMode != EArdaShaderCompilationMode::LoadOnly)
        {
            const FArdaShaderCompileResult CompileResult =
                EnsureRegisteredShaderArtifact(
                    Type, mTarget.mBackendName.c_str(),
                    Shader.mPermutationId, mDirectory);
            if (!CompileResult)
            {
                const FArdaShaderCompileDiagnostic& CompilerDiagnostic =
                    CompileResult.mDiagnostics.front();
                const bool Missing =
                    CompilerDiagnostic.mCode == EArdaShaderCompileError::ArtifactMissing ||
                    CompilerDiagnostic.mCode == EArdaShaderCompileError::ArtifactOutdated;
                auto Diagnostic = MakeDiagnostic(
                    Missing
                        ? EArdaGlobalShaderMapError::BytecodeMissing
                        : EArdaGlobalShaderMapError::ArtifactCompileFailed,
                    Type.GetName(), Path, CompilerDiagnostic.mMessage);
                if (!CompilerDiagnostic.mSourcePath.empty())
                    Diagnostic.mPhysicalSource =
                        ToEastlString(CompilerDiagnostic.mSourcePath);
                AttachSource(Diagnostic, Type);
                mDiagnostics.push_back(eastl::move(Diagnostic));
                mLoadStates[Index] = 0;
                return false;
            }
        }

        FArdaShaderBytecodeResult Bytecode = LoadShaderBytecode(Path);
        if (!Bytecode)
        {
            Bytecode.mDiagnostic.mShaderType = Type.GetName();
            AttachSource(Bytecode.mDiagnostic, Type);
            mDiagnostics.push_back(eastl::move(Bytecode.mDiagnostic));
            mLoadStates[Index] = 0;
            return false;
        }

        rhi::FArdaRHIShaderDesc ShaderDesc;
        ShaderDesc.mStage = Type.GetStage();
        ShaderDesc.mBytecode = Bytecode.mBytecode.data();
        ShaderDesc.mBytecodeSize = Bytecode.mBytecode.size();
        ShaderDesc.mEntryPoint = Type.GetEntryPoint();
        ShaderDesc.mDebugName = Type.GetName();
        auto ShaderResult = mDevice->CreateShader(ShaderDesc);
        if (!ShaderResult)
        {
            auto Diagnostic = MakeDiagnostic(
                EArdaGlobalShaderMapError::ShaderCreationFailed,
                Type.GetName(), Path, ShaderResult.mStatus.mMessage);
            AttachSource(Diagnostic, Type);
            mDiagnostics.push_back(eastl::move(Diagnostic));
            mLoadStates[Index] = 0;
            return false;
        }

        eastl::vector<rhi::FArdaRHIBindingLayoutRef> Layouts;
        if (const FArdaShaderParameterMetadata* Metadata =
                Type.GetParameterMetadata())
        {
            eastl::vector<rhi::FArdaRHIBindingLayoutDesc> LayoutDescs;
            const FArdaShaderStructStatus LayoutStatus =
                Metadata->BuildBindingLayoutDescs(LayoutDescs);
            if (!LayoutStatus)
            {
                auto Diagnostic = MakeDiagnostic(
                    EArdaGlobalShaderMapError::LayoutCreationFailed,
                    Type.GetName(), Path, LayoutStatus.mMessage);
                AttachSource(Diagnostic, Type);
                mDiagnostics.push_back(eastl::move(Diagnostic));
                mLoadStates[Index] = 0;
                return false;
            }
            for (const auto& LayoutDesc : LayoutDescs)
            {
                auto LayoutResult = mDevice->CreateBindingLayout(LayoutDesc);
                if (!LayoutResult)
                {
                    auto Diagnostic = MakeDiagnostic(
                        EArdaGlobalShaderMapError::LayoutCreationFailed,
                        Type.GetName(), Path, LayoutResult.mStatus.mMessage);
                    AttachSource(Diagnostic, Type);
                    mDiagnostics.push_back(eastl::move(Diagnostic));
                    mLoadStates[Index] = 0;
                    return false;
                }
                Layouts.push_back(eastl::move(LayoutResult.mValue));
            }
        }
        Shader.mShader = eastl::move(ShaderResult.mValue);
        Shader.mBindingLayouts = eastl::move(Layouts);
        mLoadStates[Index] = 1;
        return true;
    }

    const FArdaGlobalShaderInstance* FArdaGlobalShaderMap::Find(
        const FArdaShaderType& Type,
        uint32_t PermutationId) const
    {
        std::lock_guard<std::mutex> Lock(mLoadMutex);
        for (size_t Index = 0; Index < mShaders.size(); ++Index)
        {
            const FArdaGlobalShaderInstance& Shader = mShaders[Index];
            if (Shader.GetType().GetIdentityHash() == Type.GetIdentityHash() &&
                eastl::string(Shader.GetType().GetName()) == Type.GetName() &&
                Shader.GetPermutationId() == PermutationId)
                return EnsureSlotLoadedLocked(Index) ? &mShaders[Index] : nullptr;
        }
        return nullptr;
    }

    const FArdaGlobalShaderInstance* FArdaGlobalShaderMap::Find(
        const eastl::string& Name,
        uint32_t PermutationId) const
    {
        std::lock_guard<std::mutex> Lock(mLoadMutex);
        for (size_t Index = 0; Index < mShaders.size(); ++Index)
        {
            const FArdaGlobalShaderInstance& Shader = mShaders[Index];
            if (Name == Shader.GetType().GetName() &&
                Shader.GetPermutationId() == PermutationId)
                return EnsureSlotLoadedLocked(Index) ? &mShaders[Index] : nullptr;
        }
        return nullptr;
    }

    eastl::vector<FArdaGlobalShaderInstance>
    FArdaGlobalShaderMap::Enumerate() const
    {
        std::lock_guard<std::mutex> Lock(mLoadMutex);
        for (size_t Index = 0; Index < mShaders.size(); ++Index)
            (void)EnsureSlotLoadedLocked(Index);
        return mShaders;
    }

    eastl::vector<FArdaGlobalShaderMapDiagnostic>
    FArdaGlobalShaderMap::GetDiagnostics() const
    {
        std::lock_guard<std::mutex> Lock(mLoadMutex);
        return mDiagnostics;
    }

    bool FArdaGlobalShaderMap::IsInitialized() const noexcept
    {
        std::lock_guard<std::mutex> Lock(mLoadMutex);
        return mbInitialized;
    }

    void FArdaGlobalShaderMap::Reset() noexcept
    {
        std::lock_guard<std::mutex> Lock(mLoadMutex);
        mShaders.clear();
        mLoadStates.clear();
        mDiagnostics.clear();
        mDevice = nullptr;
        mTarget = {};
        mMode = EArdaShaderCompilationMode::OnDemand;
        mDirectory.clear();
        mbInitialized = false;
    }
}
