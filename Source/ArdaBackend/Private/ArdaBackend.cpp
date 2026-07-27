#include "ArdaBackendPch.h"

#include "ArdaBackend.h"
#include "ArdaBackendDevice.h"

namespace arda::backend
{
    ARDA_DEFINE_LOG_CATEGORY_NAMED(LogArdaBackend, "ArdaBackend", Log);

    namespace
    {
        class FArdaDefaultMessageCallback final : public nvrhi::IMessageCallback
        {
        public:
            void message(nvrhi::MessageSeverity severity, const char* messageText) override
            {
                switch (severity)
                {
                case nvrhi::MessageSeverity::Warning:
                    ARDA_LOG(
                        LogArdaBackend,
                        Warning,
                        "%s",
                        messageText ? messageText : "");
                    break;
                case nvrhi::MessageSeverity::Error:
                    ARDA_LOG(
                        LogArdaBackend,
                        Error,
                        "%s",
                        messageText ? messageText : "");
                    break;
                case nvrhi::MessageSeverity::Fatal:
                    ARDA_LOG(
                        LogArdaBackend,
                        Fatal,
                        "%s",
                        messageText ? messageText : "");
                    break;
                default:
                    ARDA_LOG(
                        LogArdaBackend,
                        Log,
                        "%s",
                        messageText ? messageText : "");
                    break;
                }
            }
        };

        EArdaBackendType currentBackend = DefaultBackend;

        struct FArdaBackendState
        {
            std::mutex mMutex;
            FArdaBackendConfiguration mConfiguration;
            FArdaDeviceContext mContext;
            FArdaDefaultMessageCallback mDefaultMessageCallback;
            eastl::unique_ptr<IArdaBackendDevice> mBackendDevice;
            eastl::string mError;
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
            switch (Configuration.mBackend)
            {
            case EArdaBackendType::D3D12:
#if defined(_WIN32)
                State.mBackendDevice = CreateD3D12BackendDevice();
#else
                State.mError = "D3D12 is only supported on Windows.";
                return false;
#endif
                break;
            case EArdaBackendType::Vulkan:
                State.mBackendDevice = CreateVulkanBackendDevice();
                break;
            }

            if (!State.mBackendDevice)
            {
                State.mError = "Failed to create the requested backend.";
                return false;
            }

            return true;
        }

        void PublishInitializedDevice(
            FArdaBackendState& State,
            const FArdaBackendConfiguration& Configuration)
        {
            State.mContext.mDevice = State.mBackendDevice->GetDevice();
            State.mContext.mBackend = Configuration.mBackend;
            State.mContext.mQueueCapabilities =
                State.mBackendDevice->GetQueueCapabilities();
            currentBackend = Configuration.mBackend;
            State.mError.clear();
        }
    }

    const EArdaBackendType& gCurrentBackend = currentBackend;

    bool FArdaQueueCapabilities::IsQueueAvailable(nvrhi::CommandQueue Queue) const noexcept
    {
        switch (Queue)
        {
        case nvrhi::CommandQueue::Graphics:
            return mbGraphics;
        case nvrhi::CommandQueue::Compute:
            return mbCompute;
        case nvrhi::CommandQueue::Copy:
            return mbCopy;
        case nvrhi::CommandQueue::Count:
            return false;
        }
        return false;
    }

    bool ConfigureBackend(const FArdaBackendConfiguration& configuration)
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        if (state.mBackendDevice)
        {
            state.mError = "The backend cannot be reconfigured after initialization.";
            return false;
        }

#if !defined(_WIN32)
        if (configuration.mBackend == EArdaBackendType::D3D12)
        {
            state.mError = "D3D12 is only supported on Windows.";
            return false;
        }
#endif

        state.mConfiguration = configuration;
        state.mContext.mBackend = configuration.mBackend;
        currentBackend = configuration.mBackend;
        state.mError.clear();
        return true;
    }

    bool ConfigureBackend(EArdaBackendType backend)
    {
        auto configuration = GetBackendConfiguration();
        configuration.mBackend = backend;
        return ConfigureBackend(configuration);
    }

    const FArdaBackendConfiguration& GetBackendConfiguration() noexcept
    {
        return GetState().mConfiguration;
    }

    bool InitializeBackend()
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        if (state.mBackendDevice)
        {
            return state.mContext.mDevice != nullptr;
        }

        FArdaBackendConfiguration runtimeConfiguration = state.mConfiguration;
        if (!runtimeConfiguration.mMessageCallback)
        {
            runtimeConfiguration.mMessageCallback = &state.mDefaultMessageCallback;
        }

        if (!CreateConfiguredDevice(state, runtimeConfiguration))
        {
            return false;
        }

        if (state.mBackendDevice->Initialize(runtimeConfiguration, nullptr) !=
            EArdaInitializeResult::Success)
        {
            state.mError = state.mBackendDevice->GetError();
            state.mBackendDevice.reset();
            return false;
        }

        PublishInitializedDevice(state, runtimeConfiguration);
        return true;
    }

    EArdaInitializeResult InitializeBackendForPresentation(
        IArdaWindowSurface& WindowSurface,
        uint32_t Width,
        uint32_t Height,
        eastl::unique_ptr<IArdaSwapChain>& OutSwapChain)
    {
        OutSwapChain.reset();

        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        if (state.mBackendDevice)
        {
            state.mError = "The backend is already initialized.";
            return EArdaInitializeResult::Failure;
        }
        if (Width == 0 || Height == 0)
        {
            state.mError = "Presentation dimensions must be non-zero.";
            return EArdaInitializeResult::Failure;
        }

        FArdaBackendConfiguration runtimeConfiguration = state.mConfiguration;
        if (!runtimeConfiguration.mMessageCallback)
        {
            runtimeConfiguration.mMessageCallback = &state.mDefaultMessageCallback;
        }

        if (!CreateConfiguredDevice(state, runtimeConfiguration))
        {
            return runtimeConfiguration.mBackend == EArdaBackendType::D3D12
                ? EArdaInitializeResult::Unavailable
                : EArdaInitializeResult::Failure;
        }

        const EArdaInitializeResult result =
            state.mBackendDevice->Initialize(runtimeConfiguration, &WindowSurface);
        if (result != EArdaInitializeResult::Success)
        {
            state.mError = state.mBackendDevice->GetError();
            state.mBackendDevice.reset();
            return result;
        }

        OutSwapChain = state.mBackendDevice->CreateSwapChain(Width, Height);
        if (!OutSwapChain)
        {
            state.mError = state.mBackendDevice->GetError();
            state.mBackendDevice.reset();
            return EArdaInitializeResult::Failure;
        }

        PublishInitializedDevice(state, runtimeConfiguration);
        return EArdaInitializeResult::Success;
    }

    void ShutdownBackend() noexcept
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        if (state.mBackendDevice)
        {
            state.mBackendDevice->WaitForIdle();
        }
        state.mContext.mDevice = nullptr;
        state.mContext.mQueueCapabilities = {};
        state.mBackendDevice.reset();
    }

    bool IsBackendInitialized() noexcept
    {
        return GetState().mContext.mDevice != nullptr;
    }

    const FArdaDeviceContext& GetDeviceContext() noexcept
    {
        return GetState().mContext;
    }

    const FArdaQueueCapabilities& GetQueueCapabilities() noexcept
    {
        return GetState().mContext.mQueueCapabilities;
    }

    nvrhi::DeviceHandle GetDevice() noexcept
    {
        return GetState().mContext.mDevice;
    }

    eastl::string GetBackendError()
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        return state.mError;
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
