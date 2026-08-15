/** @file ArdaGlobalShaderMap.h
 *  @brief Declares compiled shader loading and the device-bound global shader map.
 */
#pragma once

#include "ArdaDevice.h"
#include "ShaderStructs/ArdaShaderType.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <filesystem>

namespace arda::backend
{
    /** Identifies failures while loading or creating global shaders. */
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

    /** Describes one global shader loading or creation diagnostic. */
    struct FArdaGlobalShaderMapDiagnostic
    {
        /** Global shader map result code. */
        EArdaGlobalShaderMapError mCode = EArdaGlobalShaderMapError::None;
        /** Registered shader type associated with the diagnostic. */
        eastl::string mShaderType;
        /** Artifact path associated with the diagnostic. */
        eastl::string mPath;
        /** Virtual shader source associated with the diagnostic. */
        eastl::string mVirtualSource;
        /** Physical shader source associated with the diagnostic. */
        eastl::string mPhysicalSource;
        /** Human-readable diagnostic message. */
        eastl::string mMessage;
    };

    /** Contains loaded shader bytecode or a failure diagnostic. */
    struct FArdaShaderBytecodeResult
    {
        /** Raw compiled shader artifact bytes. */
        eastl::vector<uint8_t> mBytecode;
        /** Diagnostic produced while loading the artifact. */
        FArdaGlobalShaderMapDiagnostic mDiagnostic;
        /** @return True when non-error bytecode was loaded. */
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mDiagnostic.mCode == EArdaGlobalShaderMapError::None;
        }
    };

    /**
     * Returns the centralized artifact extension for a backend.
     * @param Backend Backend whose artifact extension is requested.
     * @return Stable null-terminated artifact extension.
     */
    [[nodiscard]] const char* GetShaderArtifactExtension(EArdaBackendType Backend) noexcept;

    /**
     * Loads one non-empty shader artifact without interpreting its contents.
     * @param Path Artifact file to load.
     * @return Loaded bytes or a diagnostic describing the failure.
     */
    [[nodiscard]] FArdaShaderBytecodeResult LoadShaderBytecode(
        const std::filesystem::path& Path);

    /** Owns one created global shader and its binding layouts. */
    class FArdaGlobalShaderInstance final
    {
    public:
        /** @return The registered type used to create this instance. */
        [[nodiscard]] const FArdaShaderType& GetType() const noexcept { return *mType; }
        /** @return The created RHI shader. */
        [[nodiscard]] const rhi::FArdaRHIShaderRef& GetShader() const noexcept { return mShader; }
        /** @return Binding layouts created for the shader parameters. */
        [[nodiscard]] const eastl::vector<rhi::FArdaRHIBindingLayoutRef>&
        GetBindingLayouts() const noexcept { return mBindingLayouts; }
        /** @return Parameter metadata, or null for a parameterless shader. */
        [[nodiscard]] const FArdaShaderParameterMetadata* GetParameterMetadata() const
        {
            return mType != nullptr ? mType->GetParameterMetadata() : nullptr;
        }

    private:
        friend class FArdaGlobalShaderMap;
        /** Registered type used to create the instance. */
        const FArdaShaderType* mType = nullptr;
        /** Created RHI shader. */
        rhi::FArdaRHIShaderRef mShader;
        /** Binding layouts created from the parameter metadata. */
        eastl::vector<rhi::FArdaRHIBindingLayoutRef> mBindingLayouts;
    };

    /** Loads, creates, and indexes all committed global shaders for one device. */
    class FArdaGlobalShaderMap final
    {
    public:
        /**
         * Loads every committed global shader.
         *
         * The same context/path is idempotent and pointer-stable. A successful
         * map must be explicitly Reset before changing either input.
         * @param DeviceContext Device and backend used to create shaders.
         * @param ShaderDirectory Directory containing compiled shader artifacts.
         * @return True when all global shaders were initialized successfully.
         */
        [[nodiscard]] bool Initialize(
            const FArdaDeviceContext& DeviceContext,
            const std::filesystem::path& ShaderDirectory);

        /** @return True when the map contains initialized global shaders. */
        [[nodiscard]] bool IsInitialized() const noexcept { return mbInitialized; }
        /**
         * Finds a shader instance by registered type.
         * @param Type Shader type to locate.
         * @return The matching instance, or null.
         */
        [[nodiscard]] const FArdaGlobalShaderInstance* Find(const FArdaShaderType& Type) const noexcept;
        /**
         * Finds a shader instance by registered name.
         * @param Name Shader type name to locate.
         * @return The matching instance, or null.
         */
        [[nodiscard]] const FArdaGlobalShaderInstance* Find(const eastl::string& Name) const noexcept;
        /** @return All initialized global shader instances. */
        [[nodiscard]] const eastl::vector<FArdaGlobalShaderInstance>& Enumerate() const noexcept
        {
            return mShaders;
        }
        /** @return Diagnostics collected during the most recent initialization. */
        [[nodiscard]] const eastl::vector<FArdaGlobalShaderMapDiagnostic>&
        GetDiagnostics() const noexcept { return mDiagnostics; }

        /** Releases all shader instances and permits initialization with new inputs. */
        void Reset() noexcept;

    private:
        /** Device permanently associated with initialized shader instances. */
        rhi::FArdaRHIDeviceRef mDevice;
        /** Backend used to choose artifact formats. */
        EArdaBackendType mBackend = DefaultBackend;
        /** Directory containing the loaded compiled artifacts. */
        std::filesystem::path mDirectory;
        /** Initialized global shader instances. */
        eastl::vector<FArdaGlobalShaderInstance> mShaders;
        /** Diagnostics from the most recent initialization. */
        eastl::vector<FArdaGlobalShaderMapDiagnostic> mDiagnostics;
        /** Whether the map is initialized. */
        bool mbInitialized = false;
    };
}
