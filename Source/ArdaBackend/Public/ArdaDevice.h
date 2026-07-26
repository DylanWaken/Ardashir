#pragma once

#include <nvrhi/nvrhi.h>

#include <string>

namespace arda::backend
{
    /** Selects the graphics API used by the backend. */
    enum class EArdaBackendType
    {
        /** Uses the Direct3D 12 backend. */
        D3D12,
        /** Uses the Vulkan backend. */
        Vulkan
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

    /** Configures backend selection, validation, and diagnostic reporting. */
    struct FArdaBackendConfiguration
    {
        /** The graphics API to initialize. */
        EArdaBackendType mBackend = DefaultBackend;
        /** Whether graphics API validation layers are enabled. */
        bool mbEnableValidation = true;
        /** Receives NVRHI diagnostic messages, or null to use no callback. */
        nvrhi::IMessageCallback* mMessageCallback = nullptr;
    };

    /** Provides the initialized NVRHI device and its backend type. */
    struct FArdaDeviceContext
    {
        /** The initialized NVRHI device handle. */
        nvrhi::DeviceHandle mDevice;
        /** The graphics API that owns the device. */
        EArdaBackendType mBackend = DefaultBackend;
    };

    /** Read-only process-wide view of the configured backend type. */
    extern const EArdaBackendType& gCurrentBackend;

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
    /** Returns the current process-wide backend configuration. */
    [[nodiscard]] const FArdaBackendConfiguration& GetBackendConfiguration() noexcept;

    /** Initializes a headless backend and reports whether it succeeded. */
    [[nodiscard]] bool InitializeBackend();

    /** Releases the process-wide backend and its device resources. */
    void ShutdownBackend() noexcept;
    
    /** Returns whether the process-wide backend is initialized. */
    [[nodiscard]] bool IsBackendInitialized() noexcept;

    /** Returns the stable process-wide device context. */
    [[nodiscard]] const FArdaDeviceContext& GetDeviceContext() noexcept;

    /** Returns the initialized NVRHI device, or an empty handle if unavailable. */
    [[nodiscard]] nvrhi::DeviceHandle GetDevice() noexcept;

    /** Returns the most recent backend error message. */
    [[nodiscard]] std::string GetBackendError();
    /**
     * Returns a readable name for a backend type.
     * @param backend The backend type to name.
     */
    [[nodiscard]] const char* ToString(EArdaBackendType backend) noexcept;

    /** Returns the stable name of the backend module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
