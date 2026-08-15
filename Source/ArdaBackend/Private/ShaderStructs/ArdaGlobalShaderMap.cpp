#include "ShaderStructs/ArdaGlobalShaderMap.h"

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
    }

    const char* GetShaderArtifactExtension(EArdaBackendType Backend) noexcept
    {
        return Backend == EArdaBackendType::Vulkan ? ".spv" : ".dxil";
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
        if (mbInitialized &&
            mDevice == DeviceContext.mDevice &&
            mBackend == DeviceContext.mBackend &&
            mDirectory == ShaderDirectory)
        {
            return true;
        }
        if (mbInitialized)
        {
            mDiagnostics.push_back(MakeDiagnostic(
                EArdaGlobalShaderMapError::ResetRequired,
                nullptr,
                ShaderDirectory,
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

        eastl::vector<FArdaGlobalShaderInstance> Loaded;
        for (const FArdaShaderType* Type :
             FArdaShaderTypeRegistration::Enumerate())
        {
            const std::filesystem::path Path =
                ShaderDirectory /
                (std::string(Type->GetOutputStem()) +
                 GetShaderArtifactExtension(DeviceContext.mBackend));
            FArdaShaderBytecodeResult Bytecode = LoadShaderBytecode(Path);
            if (!Bytecode)
            {
                Bytecode.mDiagnostic.mShaderType = Type->GetName();
                mDiagnostics.push_back(eastl::move(Bytecode.mDiagnostic));
                return false;
            }

            rhi::FArdaRHIShaderDesc ShaderDesc;
            ShaderDesc.mStage = Type->GetStage();
            ShaderDesc.mBytecode = Bytecode.mBytecode.data();
            ShaderDesc.mBytecodeSize = Bytecode.mBytecode.size();
            ShaderDesc.mEntryPoint = Type->GetEntryPoint();
            ShaderDesc.mDebugName = Type->GetName();
            auto ShaderResult = DeviceContext.mDevice->CreateShader(ShaderDesc);
            if (!ShaderResult)
            {
                mDiagnostics.push_back(MakeDiagnostic(
                    EArdaGlobalShaderMapError::ShaderCreationFailed,
                    Type->GetName(),
                    Path,
                    ShaderResult.mStatus.mMessage));
                return false;
            }

            FArdaGlobalShaderInstance Shader;
            Shader.mType = Type;
            Shader.mShader = eastl::move(ShaderResult.mValue);
            const FArdaShaderParameterMetadata* Metadata =
                Type->GetParameterMetadata();
            if (Metadata != nullptr)
            {
                eastl::vector<rhi::FArdaRHIBindingLayoutDesc> LayoutDescs;
                const FArdaShaderStructStatus LayoutStatus =
                    Metadata->BuildBindingLayoutDescs(LayoutDescs);
                if (!LayoutStatus)
                {
                    mDiagnostics.push_back(MakeDiagnostic(
                        EArdaGlobalShaderMapError::LayoutCreationFailed,
                        Type->GetName(),
                        Path,
                        LayoutStatus.mMessage));
                    return false;
                }
                for (const rhi::FArdaRHIBindingLayoutDesc& LayoutDesc : LayoutDescs)
                {
                    auto LayoutResult =
                        DeviceContext.mDevice->CreateBindingLayout(LayoutDesc);
                    if (!LayoutResult)
                    {
                        mDiagnostics.push_back(MakeDiagnostic(
                            EArdaGlobalShaderMapError::LayoutCreationFailed,
                            Type->GetName(),
                            Path,
                            LayoutResult.mStatus.mMessage));
                        return false;
                    }
                    Shader.mBindingLayouts.push_back(
                        eastl::move(LayoutResult.mValue));
                }
            }
            Loaded.push_back(eastl::move(Shader));
        }

        mDevice = DeviceContext.mDevice;
        mBackend = DeviceContext.mBackend;
        mDirectory = ShaderDirectory;
        mShaders = eastl::move(Loaded);
        mbInitialized = true;
        return true;
    }

    const FArdaGlobalShaderInstance* FArdaGlobalShaderMap::Find(
        const FArdaShaderType& Type) const noexcept
    {
        for (const FArdaGlobalShaderInstance& Shader : mShaders)
        {
            if (&Shader.GetType() == &Type)
                return &Shader;
        }
        return nullptr;
    }

    const FArdaGlobalShaderInstance* FArdaGlobalShaderMap::Find(
        const eastl::string& Name) const noexcept
    {
        for (const FArdaGlobalShaderInstance& Shader : mShaders)
        {
            if (Name == Shader.GetType().GetName())
                return &Shader;
        }
        return nullptr;
    }

    void FArdaGlobalShaderMap::Reset() noexcept
    {
        mShaders.clear();
        mDiagnostics.clear();
        mDevice = nullptr;
        mBackend = DefaultBackend;
        mDirectory.clear();
        mbInitialized = false;
    }
}
