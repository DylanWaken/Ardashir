#include "ArdaBackendPch.h"

#include "ArdaBackend.h"
#include "ArdaBackendDevice.h"

namespace arda::backend
{
    namespace
    {
        class FArdaDefaultMessageCallback final : public nvrhi::IMessageCallback
        {
        public:
            void message(nvrhi::MessageSeverity severity, const char* messageText) override
            {
                const char* severityText = "Info";
                switch (severity)
                {
                case nvrhi::MessageSeverity::Warning:
                    severityText = "Warning";
                    break;
                case nvrhi::MessageSeverity::Error:
                    severityText = "Error";
                    break;
                case nvrhi::MessageSeverity::Fatal:
                    severityText = "Fatal";
                    break;
                default:
                    break;
                }

                std::fprintf(
                    stderr,
                    "[ArdaBackend][%s] %s\n",
                    severityText,
                    messageText ? messageText : "");
            }
        };

        EArdaBackendType currentBackend = DefaultBackend;

        struct FArdaBackendState
        {
            std::mutex mutex;
            FArdaBackendConfiguration configuration;
            FArdaDeviceContext context;
            FArdaDefaultMessageCallback defaultMessageCallback;
            std::unique_ptr<IArdaBackendDevice> backendDevice;
            std::string error;
        };

        FArdaBackendState& GetState()
        {
            static FArdaBackendState state;
            return state;
        }

        bool CreateConfiguredDevice(
            FArdaBackendState& State,
            const FArdaBackendConfiguration& Configuration)
        {
            switch (Configuration.backend)
            {
            case EArdaBackendType::D3D12:
#if defined(_WIN32)
                State.backendDevice = CreateD3D12BackendDevice();
#else
                State.error = "D3D12 is only supported on Windows.";
                return false;
#endif
                break;
            case EArdaBackendType::Vulkan:
                State.backendDevice = CreateVulkanBackendDevice();
                break;
            }

            if (!State.backendDevice)
            {
                State.error = "Failed to create the requested backend.";
                return false;
            }

            return true;
        }

        void PublishInitializedDevice(
            FArdaBackendState& State,
            const FArdaBackendConfiguration& Configuration)
        {
            State.context.device = State.backendDevice->GetDevice();
            State.context.backend = Configuration.backend;
            currentBackend = Configuration.backend;
            State.error.clear();
        }
    }

    const EArdaBackendType& gCurrentBackend = currentBackend;

    bool ConfigureBackend(const FArdaBackendConfiguration& configuration)
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.backendDevice)
        {
            state.error = "The backend cannot be reconfigured after initialization.";
            return false;
        }

#if !defined(_WIN32)
        if (configuration.backend == EArdaBackendType::D3D12)
        {
            state.error = "D3D12 is only supported on Windows.";
            return false;
        }
#endif

        state.configuration = configuration;
        state.context.backend = configuration.backend;
        currentBackend = configuration.backend;
        state.error.clear();
        return true;
    }

    bool ConfigureBackend(EArdaBackendType backend)
    {
        auto configuration = GetBackendConfiguration();
        configuration.backend = backend;
        return ConfigureBackend(configuration);
    }

    const FArdaBackendConfiguration& GetBackendConfiguration() noexcept
    {
        return GetState().configuration;
    }

    bool InitializeBackend()
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.backendDevice)
        {
            return state.context.device != nullptr;
        }

        FArdaBackendConfiguration runtimeConfiguration = state.configuration;
        if (!runtimeConfiguration.messageCallback)
        {
            runtimeConfiguration.messageCallback = &state.defaultMessageCallback;
        }

        if (!CreateConfiguredDevice(state, runtimeConfiguration))
        {
            return false;
        }

        if (state.backendDevice->Initialize(runtimeConfiguration, nullptr) !=
            EArdaInitializeResult::Success)
        {
            state.error = state.backendDevice->GetError();
            state.backendDevice.reset();
            return false;
        }

        PublishInitializedDevice(state, runtimeConfiguration);
        return true;
    }

    EArdaInitializeResult InitializeBackendForPresentation(
        IArdaWindowSurface& WindowSurface,
        uint32_t Width,
        uint32_t Height,
        std::unique_ptr<IArdaSwapChain>& OutSwapChain)
    {
        OutSwapChain.reset();

        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.backendDevice)
        {
            state.error = "The backend is already initialized.";
            return EArdaInitializeResult::Failure;
        }
        if (Width == 0 || Height == 0)
        {
            state.error = "Presentation dimensions must be non-zero.";
            return EArdaInitializeResult::Failure;
        }

        FArdaBackendConfiguration runtimeConfiguration = state.configuration;
        if (!runtimeConfiguration.messageCallback)
        {
            runtimeConfiguration.messageCallback = &state.defaultMessageCallback;
        }

        if (!CreateConfiguredDevice(state, runtimeConfiguration))
        {
            return runtimeConfiguration.backend == EArdaBackendType::D3D12
                ? EArdaInitializeResult::Unavailable
                : EArdaInitializeResult::Failure;
        }

        const EArdaInitializeResult result =
            state.backendDevice->Initialize(runtimeConfiguration, &WindowSurface);
        if (result != EArdaInitializeResult::Success)
        {
            state.error = state.backendDevice->GetError();
            state.backendDevice.reset();
            return result;
        }

        OutSwapChain = state.backendDevice->CreateSwapChain(Width, Height);
        if (!OutSwapChain)
        {
            state.error = state.backendDevice->GetError();
            state.backendDevice.reset();
            return EArdaInitializeResult::Failure;
        }

        PublishInitializedDevice(state, runtimeConfiguration);
        return EArdaInitializeResult::Success;
    }

    void ShutdownBackend() noexcept
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.backendDevice)
        {
            state.backendDevice->WaitForIdle();
        }
        state.context.device = nullptr;
        state.backendDevice.reset();
    }

    bool IsBackendInitialized() noexcept
    {
        return GetState().context.device != nullptr;
    }

    const FArdaDeviceContext& GetDeviceContext() noexcept
    {
        return GetState().context;
    }

    nvrhi::DeviceHandle GetDevice() noexcept
    {
        return GetState().context.device;
    }

    std::string GetBackendError()
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.error;
    }

    const char* ToString(EArdaBackendType backend) noexcept
    {
        switch (backend)
        {
        case EArdaBackendType::D3D12:
            return "D3D12";
        case EArdaBackendType::Vulkan:
            return "Vulkan";
        }
        return "Unknown";
    }

    const char* GetModuleName() noexcept
    {
        return "ArdaBackend";
    }
}
