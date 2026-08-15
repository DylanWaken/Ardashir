#pragma once

#include "ArdaDevice.h"
#include "ShaderStructs/ArdaShaderType.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <filesystem>

namespace arda::backend
{
    enum class EArdaGlobalShaderMapError : uint8_t
    {
        None,
        RegistrationFailed,
        InvalidDevice,
        BytecodeMissing,
        BytecodeEmpty,
        BytecodeReadFailed,
        ShaderCreationFailed,
        LayoutCreationFailed,
        ResetRequired
    };

    struct FArdaGlobalShaderMapDiagnostic
    {
        EArdaGlobalShaderMapError mCode = EArdaGlobalShaderMapError::None;
        eastl::string mShaderType;
        eastl::string mPath;
        eastl::string mMessage;
    };

    struct FArdaShaderBytecodeResult
    {
        eastl::vector<uint8_t> mBytecode;
        FArdaGlobalShaderMapDiagnostic mDiagnostic;
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mDiagnostic.mCode == EArdaGlobalShaderMapError::None;
        }
    };

    /** Returns the centralized artifact extension for a backend. */
    [[nodiscard]] const char* GetShaderArtifactExtension(EArdaBackendType Backend) noexcept;

    /** Loads one non-empty shader artifact without interpreting its contents. */
    [[nodiscard]] FArdaShaderBytecodeResult LoadShaderBytecode(
        const std::filesystem::path& Path);

    class FArdaGlobalShaderInstance final
    {
    public:
        [[nodiscard]] const FArdaShaderType& GetType() const noexcept { return *mType; }
        [[nodiscard]] const rhi::FArdaRHIShaderRef& GetShader() const noexcept { return mShader; }
        [[nodiscard]] const eastl::vector<rhi::FArdaRHIBindingLayoutRef>&
        GetBindingLayouts() const noexcept { return mBindingLayouts; }
        [[nodiscard]] const FArdaShaderParameterMetadata* GetParameterMetadata() const
        {
            return mType != nullptr ? mType->GetParameterMetadata() : nullptr;
        }

    private:
        friend class FArdaGlobalShaderMap;
        const FArdaShaderType* mType = nullptr;
        rhi::FArdaRHIShaderRef mShader;
        eastl::vector<rhi::FArdaRHIBindingLayoutRef> mBindingLayouts;
    };

    class FArdaGlobalShaderMap final
    {
    public:
        /**
         * Loads every committed global shader.
         *
         * The same context/path is idempotent and pointer-stable. A successful
         * map must be explicitly Reset before changing either input.
         */
        [[nodiscard]] bool Initialize(
            const FArdaDeviceContext& DeviceContext,
            const std::filesystem::path& ShaderDirectory);

        [[nodiscard]] bool IsInitialized() const noexcept { return mbInitialized; }
        [[nodiscard]] const FArdaGlobalShaderInstance* Find(const FArdaShaderType& Type) const noexcept;
        [[nodiscard]] const FArdaGlobalShaderInstance* Find(const eastl::string& Name) const noexcept;
        [[nodiscard]] const eastl::vector<FArdaGlobalShaderInstance>& Enumerate() const noexcept
        {
            return mShaders;
        }
        [[nodiscard]] const eastl::vector<FArdaGlobalShaderMapDiagnostic>&
        GetDiagnostics() const noexcept { return mDiagnostics; }

        void Reset() noexcept;

    private:
        rhi::FArdaRHIDeviceRef mDevice;
        EArdaBackendType mBackend = DefaultBackend;
        std::filesystem::path mDirectory;
        eastl::vector<FArdaGlobalShaderInstance> mShaders;
        eastl::vector<FArdaGlobalShaderMapDiagnostic> mDiagnostics;
        bool mbInitialized = false;
    };
}
