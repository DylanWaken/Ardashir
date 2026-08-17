/** @file ArdaBackendProvider.h
 *  @brief Declares the provider contract implemented by linkable backend modules.
 */
#pragma once

#include "ArdaDevice.h"
#include "ArdaSwapChain.h"

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <cstdint>
#include <filesystem>

namespace arda::backend
{
    class IArdaExternalDeviceProvider;

    /** C++ provider contract version required by this ArdaBackend build. */
    inline constexpr uint32_t ArdaBackendProviderInterfaceVersion = 1;

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

    /** Defines the runtime device supplied by a backend module. */
    class IArdaBackendDevice
    {
    public:
        /** Destroys the device after Arda has released its RHI and swap-chain references. */
        virtual ~IArdaBackendDevice() = default;

        /**
         * Initializes an owned or externally supplied backend device.
         * @param Configuration Process configuration selected for this module.
         * @param WindowSurface Optional presentation surface.
         * @param ExternalProvider Provider selected for ExternalProvider mode, or null.
         * @return Initialization outcome.
         */
        [[nodiscard]] virtual EArdaInitializeResult Initialize(
            const FArdaBackendConfiguration& Configuration,
            IArdaWindowSurface* WindowSurface,
            const IArdaExternalDeviceProvider* ExternalProvider) = 0;

        /**
         * Creates presentation resources for an initialized presentation device.
         * @param Width Initial width in pixels.
         * @param Height Initial height in pixels.
         * @return Backend swap chain, or an empty pointer on failure.
         */
        [[nodiscard]] virtual eastl::unique_ptr<IArdaSwapChain> CreateSwapChain(
            uint32_t Width,
            uint32_t Height) = 0;

        /** Blocks until all submitted backend work has completed. */
        virtual void WaitForIdle() noexcept = 0;

        /** @return Backend-neutral RHI facade implemented by this module. */
        [[nodiscard]] virtual rhi::FArdaRHIDeviceRef GetDevice() const noexcept = 0;

        /** @return Queues exposed by the initialized device. */
        [[nodiscard]] virtual FArdaQueueCapabilities GetQueueCapabilities() const noexcept = 0;

        /** @return Most recent module-owned initialization or presentation error. */
        [[nodiscard]] virtual const eastl::string& GetError() const noexcept = 0;
    };

    /** Contract implemented by every linkable Arda backend module. */
    class IArdaBackendModule
    {
    public:
        /** Destroys the module only after it has been unregistered and is no longer active. */
        virtual ~IArdaBackendModule() = default;

        /** @return Stable descriptor copied by the registry. */
        [[nodiscard]] virtual const FArdaBackendModuleDescriptor& GetDescriptor() const noexcept = 0;

        /**
         * Allocates an uninitialized device implementation.
         * @param Source Whether the device will be owned or externally supplied.
         * @return Device implementation, or empty when that source is unsupported.
         */
        [[nodiscard]] virtual eastl::unique_ptr<IArdaBackendDevice> CreateDevice(
            EArdaDeviceSource Source) = 0;

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
