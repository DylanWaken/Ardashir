/** @file ArdaGlobalShaderMap.h
 *  @brief Declares compiled shader loading and the device-bound global shader map.
 */
#pragma once

#include "ArdaBackend.h"
#include "ShaderStructs/ArdaShaderType.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <filesystem>
#include <mutex>

namespace arda::backend
{
    /** Identifies failures while loading or creating global shaders. */
    enum class EArdaGlobalShaderMapError : uint8_t
    {
        None,
        RegistrationFailed,
        InvalidDevice,
        /** Development compilation failed before bytecode could be loaded. */
        ArtifactCompileFailed,
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

    /** Returns the artifact extension declared by an exact backend module. */
    [[nodiscard]] const char* GetShaderArtifactExtension(const char* BackendName) noexcept;

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
        [[nodiscard]] const FArdaShaderType& GetType() const noexcept { return mType; }
        /** @return The created RHI shader. */
        [[nodiscard]] const rhi::FArdaRHIShaderRef& GetShader() const noexcept { return mShader; }
        /** @return True after bytecode and RHI resources were created successfully. */
        [[nodiscard]] bool IsLoaded() const noexcept { return mShader != nullptr; }
        /** @return Encoded permutation identifier used to create this instance. */
        [[nodiscard]] uint32_t GetPermutationId() const noexcept { return mPermutationId; }
        /** @return Binding layouts created for the shader parameters. */
        [[nodiscard]] const eastl::vector<rhi::FArdaRHIBindingLayoutRef>&
        GetBindingLayouts() const noexcept { return mBindingLayouts; }
        /** @return Parameter metadata, or null for a parameterless shader. */
        [[nodiscard]] const FArdaShaderParameterMetadata* GetParameterMetadata() const
        {
            return mType.GetParameterMetadata();
        }

    private:
        friend class FArdaGlobalShaderMap;
        /** Owned descriptor snapshot used to create the instance. */
        FArdaShaderType mType;
        /** Encoded permutation identifier used to create the instance. */
        uint32_t mPermutationId = 0;
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
         * @param Device Device used to create shaders. The configured backend is authoritative.
         * @param ShaderDirectory Directory containing compiled shader artifacts.
         * @return True when all global shaders were initialized successfully.
         */
        [[nodiscard]] bool Initialize(
            rhi::FArdaRHIDeviceRef Device,
            const std::filesystem::path& ShaderDirectory);
        /**
         * Initializes using the configured persistent backend shader cache.
         * @param Device Device used to create shaders.
         * @return True when registration and policy-specific initialization succeed.
         */
        [[nodiscard]] bool Initialize(rhi::FArdaRHIDeviceRef Device);

        /** @return True when the map contains initialized global shaders. */
        [[nodiscard]] bool IsInitialized() const noexcept;
        /**
         * Finds a shader instance by registered type.
         * @param Type Shader type to locate.
         * @param PermutationId Encoded permutation identifier to locate.
         * @return The matching instance, or null. Returned pointers remain
         * valid until Reset; callers must externally quiesce use before
         * Reset or Initialize changes map ownership.
         */
        [[nodiscard]] const FArdaGlobalShaderInstance* Find(
            const FArdaShaderType& Type,
            uint32_t PermutationId = 0) const;
        /**
         * Finds a shader instance by registered name.
         * @param Name Shader type name to locate.
         * @param PermutationId Encoded permutation identifier to locate.
         * @return The matching instance, or null.
         */
        [[nodiscard]] const FArdaGlobalShaderInstance* Find(
            const eastl::string& Name,
            uint32_t PermutationId = 0) const;
        /**
         * Eagerly loads every selected placeholder before returning.
         * @return All successfully loaded global shader instances. If loading
         * fails, diagnostics are recorded and unloaded slots remain present.
         */
        [[nodiscard]] eastl::vector<FArdaGlobalShaderInstance> Enumerate() const;
        /** @return Diagnostics collected during the most recent initialization. */
        [[nodiscard]] eastl::vector<FArdaGlobalShaderMapDiagnostic>
        GetDiagnostics() const;

        /**
         * Releases all shader instances and permits initialization with new
         * inputs. Reset and Initialize require external quiescence of raw
         * pointers previously returned by Find.
         */
        void Reset() noexcept;

    private:
        /** Ensures one stable slot is loaded according to the configured policy. */
        [[nodiscard]] bool EnsureSlotLoaded(size_t Index) const;
        /** Lock-held implementation; slots are preallocated before publication. */
        [[nodiscard]] bool EnsureSlotLoadedLocked(size_t Index) const;
        /** Device permanently associated with initialized shader instances. */
        rhi::FArdaRHIDeviceRef mDevice;
        /** Exact backend-module shader target used by this map. */
        FArdaShaderTarget mTarget;
        /** Compilation timing policy captured at initialization. */
        EArdaShaderCompilationMode mMode = EArdaShaderCompilationMode::OnDemand;
        /** Directory containing the loaded compiled artifacts. */
        std::filesystem::path mDirectory;
        /** Initialized global shader instances. */
        mutable eastl::vector<FArdaGlobalShaderInstance> mShaders;
        /** Per-slot state: zero unloaded, one loaded, two permanently failed. */
        mutable eastl::vector<uint8_t> mLoadStates;
        /** Diagnostics from the most recent initialization. */
        mutable eastl::vector<FArdaGlobalShaderMapDiagnostic> mDiagnostics;
        /** Serializes first-use loading and diagnostics without growing slots. */
        mutable std::mutex mLoadMutex;
        /** Whether the map is initialized. */
        bool mbInitialized = false;
    };
}
