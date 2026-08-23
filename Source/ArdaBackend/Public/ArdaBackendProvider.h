/** @file ArdaBackendProvider.h
 *  @brief Declares the provider contract implemented by linkable backend modules.
 */
#pragma once

#include "ArdaBackend.h"
#include "ArdaSwapChain.h"
#include "RHI/ArdaRHIProvider.h"

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <cstdint>
#include <filesystem>

namespace arda::backend
{
    class IArdaExternalDeviceProvider;

    /** C++ provider contract version required by this ArdaBackend build. */
    inline constexpr uint32_t ArdaBackendProviderInterfaceVersion = 3;

    /** Identifies the bytecode family consumed by a backend module. */
    enum class EArdaShaderBinaryFormat : uint8_t
    {
        /** DirectX Intermediate Language produced from HLSL. */
        Dxil,
        /** Standard Portable Intermediate Representation for Vulkan. */
        Spirv,
        /** A format whose compiler invocation is entirely backend-defined. */
        BackendDefined
    };

    /** Describes whether a module handled a shader compiler process itself. */
    enum class EArdaBackendShaderCompileResult : uint8_t
    {
        /** Core should launch the configured executable and argument list. */
        NotHandled,
        /** Module produced the requested output artifact. */
        Success,
        /** Module handled the request but compilation failed. */
        Failure
    };

    /** Describes one backend implementation available to ArdaBackend. */
    struct FArdaBackendModuleDescriptor
    {
        /** Provider contract version used to compile the module. */
        uint32_t mInterfaceVersion = ArdaBackendProviderInterfaceVersion;
        /** Stable registry key, such as "native-vulkan" or "unreal-rhi". */
        eastl::string mName;
        /** Human-readable module name used by diagnostics and tools. */
        eastl::string mDisplayName;
        /** Graphics API compatibility class used by existing shader permutations. */
        EArdaBackendType mBackendType = DefaultBackend;
        /** Shader bytecode family accepted by the module. */
        EArdaShaderBinaryFormat mShaderBinaryFormat =
            EArdaShaderBinaryFormat::BackendDefined;
        /** Artifact suffix, including the leading period. */
        eastl::string mShaderArtifactExtension;
        /** Stable cache identity for an in-process or engine-owned shader compiler. */
        eastl::string mShaderCompilerIdentity;
        /** Whether the module can create and own a native device. */
        bool mbSupportsOwnedDevice = false;
        /** Whether the module can adopt a device supplied by an external provider. */
        bool mbSupportsExternalDevice = false;
        /** Default-selection priority among modules for the same compatibility API. */
        int32_t mPriority = 0;
    };

    /** Immutable shader-facing identity resolved from a registered backend module. */
    struct FArdaShaderTarget
    {
        /** Stable module registry name. */
        eastl::string mBackendName;
        /** Compatibility class exposed to shader permutation policy. */
        EArdaBackendType mBackend = DefaultBackend;
        /** Bytecode family consumed by the module. */
        EArdaShaderBinaryFormat mBinaryFormat =
            EArdaShaderBinaryFormat::BackendDefined;
        /** Artifact suffix, including its leading period. */
        eastl::string mArtifactExtension;
        /** Stable cache identity when no compiler executable is used. */
        eastl::string mCompilerIdentity;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return !mBackendName.empty() && !mArtifactExtension.empty();
        }
    };

    /**
     * Represents a backend-owned shader compiler invocation.
     *
     * The core fills a DXC-compatible fallback invocation before offering it to
     * the selected module. A module may rewrite that command or execute the job
     * itself through InvokeShaderCompiler.
     */
    struct FArdaBackendShaderCompileInvocation
    {
        /** Source file passed to the compiler. */
        std::filesystem::path mSourcePath;
        /** Final artifact path expected by Arda's shader cache. */
        std::filesystem::path mOutputPath;
        /** Compiler executable selected by the core, or replaced by the module. */
        std::filesystem::path mCompilerExecutable;
        /** Shader entry point; empty for library profiles. */
        eastl::string mEntryPoint;
        /** Backend-neutral shader stage requested by the registered shader type. */
        rhi::EArdaRHIShaderStage mStage = rhi::EArdaRHIShaderStage::None;
        /** Backend compiler profile, such as cs_6_0 or lib_6_3. */
        eastl::string mProfile;
        /** Complete argument list excluding the executable. */
        eastl::vector<eastl::string> mArguments;
    };

    /** Result of creating presentation resources for an initialized backend device. */
    struct FArdaSwapChainCreateResult
    {
        /** Created swap chain, or empty on failure. */
        eastl::unique_ptr<IArdaSwapChain> mSwapChain;
        /** Failure diagnostic. Empty on success. */
        eastl::string mError;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mSwapChain != nullptr;
        }
    };

    /** Defines an initialized runtime device supplied by a backend module. */
    class IArdaBackendDevice
    {
    public:
        /** Destroys the device after Arda has released its RHI and swap-chain references. */
        virtual ~IArdaBackendDevice() = default;

        /**
         * Creates presentation resources for an initialized presentation device.
         * @param Width Initial width in pixels.
         * @param Height Initial height in pixels.
         * @param Device Core-owned RHI facade used to import presentation images.
         * @return Swap-chain result containing either the resource or its error.
         */
        [[nodiscard]] virtual FArdaSwapChainCreateResult CreateSwapChain(
            uint32_t Width,
            uint32_t Height,
            rhi::FArdaRHIDeviceRef Device) = 0;
    };

    /** Result of atomically creating and initializing a backend device. */
    struct FArdaBackendDeviceCreateResult
    {
        /** Initialization outcome. */
        EArdaInitializeResult mResult = EArdaInitializeResult::Failure;
        /** Initialized device. Empty unless initialization succeeded. */
        eastl::unique_ptr<IArdaBackendDevice> mBackendDevice;
        /** Provider implementation wrapped by ArdaBackend's concrete RHI device. */
        eastl::shared_ptr<rhi::provider::IArdaRHIProviderDevice> mProviderDevice;
        /** Failure diagnostic. Empty on success. */
        eastl::string mError;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mResult == EArdaInitializeResult::Success &&
                mBackendDevice != nullptr && mProviderDevice != nullptr;
        }
    };

    /** Contract implemented by every linkable Arda backend module. */
    class IArdaBackendModule
    {
    public:
        /** Destroys the module only after it has been unregistered and is no longer active. */
        virtual ~IArdaBackendModule() = default;

        /** @return Module-owned descriptor, immutable while the module is registered. */
        [[nodiscard]] virtual const FArdaBackendModuleDescriptor& GetDescriptor() const noexcept = 0;

        /**
         * Creates and initializes a device as one operation.
         * @param Configuration Process configuration selected for this module.
         * @param WindowSurface Optional presentation surface.
         * @param ExternalProvider Provider selected for ExternalProvider mode, or null.
         * @return Initialized device result.
         */
        [[nodiscard]] virtual FArdaBackendDeviceCreateResult CreateDevice(
            const FArdaBackendConfiguration& Configuration,
            IArdaWindowSurface* WindowSurface,
            const IArdaExternalDeviceProvider* ExternalProvider) = 0;

        /**
         * Allows a module to validate or replace the fallback compiler command.
         * BackendDefined shader formats must make the invocation usable by this
         * method and/or handle it through InvokeShaderCompiler. DXIL and SPIR-V
         * modules may retain the core's DXC-compatible command.
         * @param Invocation Mutable, self-contained compiler invocation.
         * @return Status describing whether the invocation can proceed.
         */
        [[nodiscard]] virtual rhi::FArdaRHIStatus ConfigureShaderCompileInvocation(
            FArdaBackendShaderCompileInvocation& Invocation) const = 0;

        /**
         * Optionally executes a compiler invocation inside the backend module.
         * This supports engine shader services and compilers whose process ABI
         * cannot be represented by the core's DXC-compatible fallback launcher.
         * @param Invocation Invocation whose output path is a temporary artifact.
         * @param OutDiagnostics Receives compiler output on failure.
         * @return Whether the module declined, succeeded, or failed.
         */
        [[nodiscard]] virtual EArdaBackendShaderCompileResult InvokeShaderCompiler(
            const FArdaBackendShaderCompileInvocation& Invocation,
            eastl::string& OutDiagnostics) const
        {
            static_cast<void>(Invocation);
            OutDiagnostics.clear();
            return EArdaBackendShaderCompileResult::NotHandled;
        }
    };

    /**
     * Registers a linkable backend module by its stable descriptor name.
     * Re-registering the same object is idempotent; name collisions are rejected.
     * @param Module Module that remains alive until it is unregistered.
     * @return True when the module is registered.
     */
    [[nodiscard]] bool RegisterBackendModule(IArdaBackendModule& Module);

    /**
     * Removes an inactive backend module from the process registry.
     * @param Module Exact module object previously registered.
     * @return True when absent or successfully removed.
     */
    [[nodiscard]] bool UnregisterBackendModule(IArdaBackendModule& Module);

    /**
     * Finds a registered backend by stable name.
     * @param Name Stable module name.
     * @return Non-owning module pointer, or null.
     */
    [[nodiscard]] IArdaBackendModule* FindBackendModule(const char* Name) noexcept;

    /**
     * Selects the highest-priority module compatible with a graphics API.
     * @param BackendType Compatibility API requested by legacy configuration.
     * @return Non-owning module pointer, or null.
     */
    [[nodiscard]] IArdaBackendModule* FindDefaultBackendModule(
        EArdaBackendType BackendType) noexcept;

    /** Resolves a registered module into an immutable shader target. */
    [[nodiscard]] bool ResolveShaderTarget(
        const char* BackendName,
        FArdaShaderTarget& OutTarget) noexcept;

    /** Resolves the highest-priority module for a compatibility class. */
    [[nodiscard]] bool ResolveDefaultShaderTarget(
        EArdaBackendType BackendType,
        FArdaShaderTarget& OutTarget) noexcept;

    /** @return Copies of all registered descriptors in stable name order. */
    [[nodiscard]] eastl::vector<FArdaBackendModuleDescriptor> EnumerateBackendModules();

    /** @return Selected module after initialization, or null. */
    [[nodiscard]] const IArdaBackendModule* GetActiveBackendModule() noexcept;
}
