/** @file ArdaBackend.h
 *  @brief Declares backend selection, device initialization, and diagnostics.
 */
#pragma once

#include "ArdaAssert.h"
#include "ArdaLog.h"
#include "RHI/ArdaRHI.h"

#include <EASTL/string.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace arda::backend
{
    /** Identifies the severity of a backend diagnostic message. */
    enum class EArdaDiagnosticSeverity : uint8_t { Info, Warning, Error, Fatal };

    /** Receives diagnostic messages emitted by the backend. */
    class IArdaDiagnosticCallback
    {
    public:
        /** Destroys the diagnostic callback. */
        virtual ~IArdaDiagnosticCallback() = default;
        /**
         * Handles a backend diagnostic message.
         * @param Severity Severity assigned to the message.
         * @param Text Null-terminated diagnostic text.
         */
        virtual void Message(EArdaDiagnosticSeverity Severity, const char* Text) = 0;
    };

    /** Stores a platform-native handle without exposing its native type. */
    struct FArdaNativeObject
    {
        /** Integer representation of the native handle. */
        uintptr_t mValue = 0;
        /** Creates an empty native object. */
        constexpr FArdaNativeObject() noexcept = default;
        /** Creates an empty native object from null. */
        constexpr FArdaNativeObject(std::nullptr_t) noexcept {}
        /**
         * Creates a native object from an integer handle.
         * @param Value Integer representation of the native handle.
         */
        explicit constexpr FArdaNativeObject(uintptr_t Value) noexcept : mValue(Value) {}
        /**
         * Creates a native object from a pointer.
         * @tparam T Pointed-to native type.
         * @param Value Native pointer to encode.
         */
        template <typename T>
        explicit FArdaNativeObject(T* Value) noexcept : mValue(reinterpret_cast<uintptr_t>(Value)) {}
        /** @return True when the native handle is nonzero. */
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return mValue != 0; }
        /**
         * Decodes the native handle as the requested type.
         * @tparam T Pointer or integer type to return.
         * @return The native handle converted to T.
         */
        template <typename T> [[nodiscard]] T As() const noexcept { return reinterpret_cast<T>(mValue); }
    };

    /** Selects the graphics API used by the backend. */
    enum class EArdaBackendType
    {
        /** Uses the Direct3D 12 backend. */
        D3D12,
        /** Uses the Vulkan backend. */
        Vulkan,
        /** Uses a module-defined API without a legacy graphics-API classification. */
        Custom
    };

    /**
     * Selects when registered shaders are compiled and loaded.
     *
     * The selected policy is fixed by ConfigureBackend before initialization
     * and applies to the active graphics backend only.
     */
    enum class EArdaShaderCompilationMode
    {
        /** Never invokes the compiler; initialized maps load deployed artifacts eagerly. */
        LoadOnly,
        /** Ensures all selected artifacts during backend startup and loads maps eagerly. */
        Startup,
        /** Defers compilation and RHI shader creation until a shader is first requested. */
        OnDemand
    };

    /** Selects whether Arda creates the native device or wraps one supplied externally. */
    enum class EArdaDeviceSource
    {
        /** Arda creates and owns the native graphics device and queues. */
        ArdaCreated,
        /** A registered external provider supplies non-owning native device handles. */
        ExternalProvider
    };

    /** Describes the outcome of backend initialization. */
    enum class EArdaInitializeResult
    {
        /** Initialization completed successfully. */
        Success,
        /** The requested backend is unavailable on this system. */
        Unavailable,
        /** Initialization failed for another reason. */
        Failure
    };

#if defined(_WIN32)
    /** The platform's preferred backend. */
    inline constexpr EArdaBackendType DefaultBackend = EArdaBackendType::D3D12;
#else
    /** The platform's preferred backend. */
    inline constexpr EArdaBackendType DefaultBackend = EArdaBackendType::Vulkan;
#endif

    /** Desktop-GPU admission profile applied during backend initialization. */
    enum class EArdaRHIDeviceProfile : uint8_t
    {
        None,
        RayTracingInfrastructure,
        RealtimeRayTracing,
        RealtimeRayTracingAndML
    };

    /** Returns the feature requirements implied by a standard device profile. */
    [[nodiscard]] inline rhi::FArdaRHIFeatureRequirements
        GetArdaRHIProfileRequirements(EArdaRHIDeviceProfile Profile) noexcept
    {
        rhi::FArdaRHIFeatureRequirements Result;
        if (Profile >= EArdaRHIDeviceProfile::RayTracingInfrastructure)
        {
            Result.mbRequireRayTracingInfrastructure = true;
            Result.mbRequireAccelerationStructures = true;
        }
        if (Profile >= EArdaRHIDeviceProfile::RealtimeRayTracing)
        {
            Result.mbRequireHardwareRayTracing = true;
            Result.mbRequireRayTracingPipelines = true;
            Result.mbRequireLocalShaderTableArguments = true;
            Result.mbRequireUnboundedDescriptors = true;
            Result.mbRequireGpuQueueWaits = true;
        }
        if (Profile >= EArdaRHIDeviceProfile::RealtimeRayTracingAndML)
        {
            Result.mbRequireNativeFloat16 = true;
            Result.mbRequireDedicatedComputeQueue = true;
        }
        return Result;
    }

    /** Configures backend selection, shader policy, validation, and diagnostics. */
    struct FArdaBackendConfiguration
    {
        /**
         * Authoritative backend module name. Empty selects the highest-priority
         * module compatible with mBackend. Accepted configurations are normalized
         * to a non-empty exact module name.
         */
        eastl::string mBackendName;
        /** Fallback selector while mBackendName is empty; otherwise its derived API class. */
        EArdaBackendType mBackend = DefaultBackend;
        /** The source from which the native graphics device is obtained. */
        EArdaDeviceSource mDeviceSource = EArdaDeviceSource::ArdaCreated;
        /** Whether graphics API validation layers are enabled. */
        bool mbEnableValidation = true;
        /** Optional RT/ML-oriented desktop-GPU admission profile. */
        EArdaRHIDeviceProfile mRequiredDeviceProfile =
            EArdaRHIDeviceProfile::None;
        /** Additional module-specific abilities required during initialization. */
        rhi::FArdaRHIFeatureRequirements mRequiredFeatures;
        /** Timing policy for registered shader compilation on the active backend. */
        EArdaShaderCompilationMode mShaderCompilationMode =
            EArdaShaderCompilationMode::OnDemand;
        /**
         * Persistent registered-shader artifact cache.
         *
         * Relative paths are resolved to stable absolute paths when the
         * configuration is accepted. The directory is created only if
         * compilation needs to publish an artifact.
         */
        std::filesystem::path mShaderCacheDirectory =
            std::filesystem::path(".arda-cache") / "shaders";
        /**
         * Persistent backend-native compiled pipeline cache.
         *
         * Blobs are backend, adapter, and driver specific. Relative paths are
         * resolved to absolute paths by ConfigureBackend. An empty path
         * explicitly disables pipeline-cache disk I/O.
         */
        std::filesystem::path mPipelineCacheDirectory =
            std::filesystem::path(".arda-cache") / "pipelines";
        /** Receives backend diagnostic messages, or null to use the default callback. */
        IArdaDiagnosticCallback* mMessageCallback = nullptr;
    };

    /**
     * Replaces the backend configuration before initialization.
     * Configuration and lifetime calls must not run concurrently with device use.
     * @param configuration The complete configuration to apply.
     * @return True when the configuration was accepted.
     */
    [[nodiscard]] bool ConfigureBackend(const FArdaBackendConfiguration& configuration);
    /**
     * Selects a backend using the remaining current configuration.
     * @param backend The graphics API to select.
     * @return True when the backend selection was accepted.
     */
    [[nodiscard]] bool ConfigureBackend(EArdaBackendType backend);
    /**
     * Selects a registered backend module by stable name.
     * @param BackendName Name returned by EnumerateBackendModules.
     * @return True when the named module exists and configuration was accepted.
     */
    [[nodiscard]] bool ConfigureBackend(const char* BackendName);
    /** @return The current process-wide backend configuration. */
    [[nodiscard]] const FArdaBackendConfiguration& GetBackendConfiguration() noexcept;

    /** @return True when a headless backend was initialized successfully. */
    [[nodiscard]] bool InitializeBackend();

    /** Releases the process-wide backend and its device resources. */
    void ShutdownBackend() noexcept;

    /** @return True when the process-wide backend is initialized. */
    [[nodiscard]] bool IsBackendInitialized() noexcept;

    /** @return The initialized opaque RHI device, or an empty reference. */
    [[nodiscard]] rhi::FArdaRHIDeviceRef GetDevice() noexcept;

    /** @return The most recent backend error message. */
    [[nodiscard]] eastl::string GetBackendError();
    /**
     * Returns a readable name for a backend type.
     * @param backend The backend type to name.
     * @return A stable null-terminated backend name.
     */
    [[nodiscard]] const char* ToString(EArdaBackendType backend) noexcept;

    /** @return The stable name of the backend module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
