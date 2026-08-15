#pragma once

#include "RHIWrappers/ArdaRHI.h"

#include <EASTL/string.h>

#include <cstddef>
#include <cstdint>
namespace arda::backend
{
    enum class EArdaDiagnosticSeverity : uint8_t { Info, Warning, Error, Fatal };

    class IArdaDiagnosticCallback
    {
    public:
        virtual ~IArdaDiagnosticCallback() = default;
        virtual void Message(EArdaDiagnosticSeverity Severity, const char* Text) = 0;
    };

    struct FArdaNativeObject
    {
        uintptr_t mValue = 0;
        constexpr FArdaNativeObject() noexcept = default;
        constexpr FArdaNativeObject(std::nullptr_t) noexcept {}
        explicit constexpr FArdaNativeObject(uintptr_t Value) noexcept : mValue(Value) {}
        template <typename T>
        explicit FArdaNativeObject(T* Value) noexcept : mValue(reinterpret_cast<uintptr_t>(Value)) {}
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return mValue != 0; }
        template <typename T> [[nodiscard]] T As() const noexcept { return reinterpret_cast<T>(mValue); }
    };
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
        /** Receives backend diagnostic messages, or null to use the default callback. */
        IArdaDiagnosticCallback* mMessageCallback = nullptr;
    };

    /** Describes which RHI command queues are available on a backend device. */
    struct FArdaQueueCapabilities
    {
        /** Whether the required graphics queue is available. */
        bool mbGraphics = false;
        /** Whether a distinct compute queue is available. */
        bool mbCompute = false;
        /** Whether a distinct copy queue is available. */
        bool mbCopy = false;

        /**
         * Returns whether a command queue is available.
         * @param Queue The RHI queue type to inspect.
         * @return True when commands can be submitted to the requested queue.
         */
        [[nodiscard]] bool IsQueueAvailable(rhi::EArdaRHIQueueType Queue) const noexcept;
    };

    /** Provides the initialized opaque RHI device and its backend type. */
    struct FArdaDeviceContext
    {
        /** The initialized opaque RHI device reference. */
        rhi::FArdaRHIDeviceRef mDevice;
        /** The graphics API that owns the device. */
        EArdaBackendType mBackend = DefaultBackend;
        /** The command queues exposed by the initialized RHI device. */
        FArdaQueueCapabilities mQueueCapabilities;
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

    /** Returns the command queues exposed by the initialized backend. */
    [[nodiscard]] const FArdaQueueCapabilities& GetQueueCapabilities() noexcept;

    /** Returns the initialized opaque RHI device, or an empty reference. */
    [[nodiscard]] rhi::FArdaRHIDeviceRef GetDevice() noexcept;

    /** Returns the most recent backend error message. */
    [[nodiscard]] eastl::string GetBackendError();
    /**
     * Returns a readable name for a backend type.
     * @param backend The backend type to name.
     */
    [[nodiscard]] const char* ToString(EArdaBackendType backend) noexcept;

    /** Returns the stable name of the backend module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
