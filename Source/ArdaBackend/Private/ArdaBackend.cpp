#include "ArdaBackendPch.h"

#include "ArdaBackend.h"
#include "ArdaBackendDevice.h"
#include "ShaderStructs/ArdaShaderDirectoriesPrivate.h"

#include <atomic>

namespace arda::backend
{
    ARDA_DEFINE_LOG_CATEGORY_NAMED(LogArdaBackend, "ArdaBackend", Log);

    namespace
    {
        class FArdaDefaultMessageCallback final : public IArdaDiagnosticCallback
        {
        public:
            void Message(EArdaDiagnosticSeverity severity, const char* messageText) override
            {
                switch (severity)
                {
                case EArdaDiagnosticSeverity::Warning:
                    ARDA_LOG(
                        LogArdaBackend,
                        Warning,
                        "%s",
                        messageText ? messageText : "");
                    break;
                case EArdaDiagnosticSeverity::Error:
                    ARDA_LOG(
                        LogArdaBackend,
                        Error,
                        "%s",
                        messageText ? messageText : "");
                    break;
                case EArdaDiagnosticSeverity::Fatal:
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
            std::atomic_bool mbInitialized{ false };
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
            State.mbInitialized.store(true, std::memory_order_release);
            State.mError.clear();
        }

        bool FreezeAndValidateShaderSources(FArdaBackendState& State)
        {
            const FArdaShaderDirectoryStatus DirectoryStatus =
                private_api::ScanAndFreezeShaderSourceDirectoriesForBackend();
            if (!DirectoryStatus)
            {
                State.mError =
                    eastl::string("Shader source directory registry failed: ") +
                    DirectoryStatus.mMessage;
                return false;
            }
            const FArdaShaderRegistrationStatus RegistrationStatus =
                FArdaShaderTypeRegistration::CommitAll();
            if (!RegistrationStatus)
            {
                State.mError =
                    eastl::string("Global shader registration failed: ") +
                    RegistrationStatus.mMessage;
                return false;
            }
            return true;
        }

        bool BeginShaderDirectoryUse(FArdaBackendState& State)
        {
            const FArdaShaderDirectoryStatus Status =
                private_api::BeginShaderDirectoryRegistryUse();
            if (Status)
                return true;
            State.mError =
                eastl::string("Shader source directory registry is unavailable: ") +
                Status.mMessage;
            return false;
        }
    }

    const EArdaBackendType& gCurrentBackend = currentBackend;

    bool FArdaQueueCapabilities::IsQueueAvailable(rhi::EArdaRHIQueueType Queue) const noexcept
    {
        switch (Queue)
        {
        case rhi::EArdaRHIQueueType::Graphics:
            return mbGraphics;
        case rhi::EArdaRHIQueueType::Compute:
            return mbCompute;
        case rhi::EArdaRHIQueueType::Copy:
            return mbCopy;
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

        if (!BeginShaderDirectoryUse(state))
            return false;
        if (!FreezeAndValidateShaderSources(state))
        {
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return false;
        }

        if (!CreateConfiguredDevice(state, runtimeConfiguration))
        {
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return false;
        }

        if (state.mBackendDevice->Initialize(runtimeConfiguration, nullptr) !=
            EArdaInitializeResult::Success)
        {
            state.mError = state.mBackendDevice->GetError();
            state.mBackendDevice.reset();
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return false;
        }

        PublishInitializedDevice(state, runtimeConfiguration);
        private_api::CompleteShaderDirectoryRegistryUse(true);
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

        if (!BeginShaderDirectoryUse(state))
            return EArdaInitializeResult::Failure;
        if (!FreezeAndValidateShaderSources(state))
        {
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return EArdaInitializeResult::Failure;
        }

        if (!CreateConfiguredDevice(state, runtimeConfiguration))
        {
            private_api::CompleteShaderDirectoryRegistryUse(false);
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
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return result;
        }

        OutSwapChain = state.mBackendDevice->CreateSwapChain(Width, Height);
        if (!OutSwapChain)
        {
            state.mError = state.mBackendDevice->GetError();
            state.mBackendDevice.reset();
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return EArdaInitializeResult::Failure;
        }

        PublishInitializedDevice(state, runtimeConfiguration);
        private_api::CompleteShaderDirectoryRegistryUse(true);
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
        state.mbInitialized.store(false, std::memory_order_release);
        private_api::ReleaseShaderDirectoryRegistryAfterShutdown();
    }

    bool IsBackendInitialized() noexcept
    {
        return GetState().mbInitialized.load(std::memory_order_acquire);
    }

    const FArdaDeviceContext& GetDeviceContext() noexcept
    {
        return GetState().mContext;
    }

    const FArdaQueueCapabilities& GetQueueCapabilities() noexcept
    {
        return GetState().mContext.mQueueCapabilities;
    }

    rhi::FArdaRHIDeviceRef GetDevice() noexcept
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
