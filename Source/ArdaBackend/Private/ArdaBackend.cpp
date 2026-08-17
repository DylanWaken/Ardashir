#include "ArdaBackendCorePch.h"

#include "ArdaBackend.h"
#include "ArdaBackendRegistry.h"
#include "ArdaLinkedBackends.h"
#include "ShaderStructs/ArdaShaderCompiler.h"
#include "ShaderStructs/ArdaShaderDirectoriesPrivate.h"

#include <atomic>

namespace arda::backend
{
    void SetBackendError(const char* Error);

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
            IArdaBackendModule* mBackendModule = nullptr;
            IArdaExternalDeviceProvider* mExternalDeviceProvider = nullptr;
            eastl::string mError;
            std::atomic_bool mbInitialized{ false };
        };

        FArdaBackendState& GetState()
        {
            static FArdaBackendState state;
            return state;
        }

        bool ResolveAndValidateRuntimeConfiguration(
            FArdaBackendState& State,
            FArdaBackendConfiguration& Configuration)
        {
            private_api::RegisterLinkedBackendModules();
            IArdaBackendModule* Module = Configuration.mBackendName.empty()
                ? FindDefaultBackendModule(Configuration.mBackend)
                : FindBackendModule(Configuration.mBackendName.c_str());
            if (!Module)
            {
                State.mError = Configuration.mBackendName.empty()
                    ? "No linked backend module supports the configured graphics API."
                    : "The configured backend module is not registered in this build.";
                return false;
            }
            const FArdaBackendModuleDescriptor& ModuleDescriptor =
                Module->GetDescriptor();
            if (!Configuration.mBackendName.empty() &&
                ModuleDescriptor.mBackendType != Configuration.mBackend)
            {
                State.mError =
                    "The configured module name and graphics API compatibility class disagree.";
                return false;
            }
            Configuration.mBackendName = ModuleDescriptor.mName;
            Configuration.mBackend = ModuleDescriptor.mBackendType;
            const bool bExternal = Configuration.mDeviceSource ==
                EArdaDeviceSource::ExternalProvider;
            if ((bExternal && !ModuleDescriptor.mbSupportsExternalDevice) ||
                (!bExternal && !ModuleDescriptor.mbSupportsOwnedDevice))
            {
                State.mError = bExternal
                    ? "The configured backend module cannot adopt external devices."
                    : "The configured backend module cannot create an owned device.";
                return false;
            }
            State.mBackendModule = Module;
            if (Configuration.mShaderCacheDirectory.empty())
            {
                State.mError = "The shader cache directory must not be empty.";
                return false;
            }
            std::error_code Error;
            Configuration.mShaderCacheDirectory = std::filesystem::absolute(
                Configuration.mShaderCacheDirectory, Error).lexically_normal();
            if (Error || Configuration.mShaderCacheDirectory.empty())
            {
                State.mError =
                    "The shader cache directory could not be resolved to an absolute path.";
                return false;
            }
            if (!Configuration.mPipelineCacheDirectory.empty())
            {
                Error.clear();
                Configuration.mPipelineCacheDirectory = std::filesystem::absolute(
                    Configuration.mPipelineCacheDirectory, Error).lexically_normal();
                if (Error || Configuration.mPipelineCacheDirectory.empty())
                {
                    State.mError =
                        "The pipeline cache directory could not be resolved to an absolute path.";
                    return false;
                }
                Error.clear();
                const bool Exists = std::filesystem::exists(
                    Configuration.mPipelineCacheDirectory, Error);
                if (Error || (Exists && !std::filesystem::is_directory(
                        Configuration.mPipelineCacheDirectory, Error)) || Error)
                {
                    State.mError =
                        "The pipeline cache path cannot be inspected or is not a directory.";
                    return false;
                }
            }
            if (Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider)
            {
                if (!State.mExternalDeviceProvider)
                {
                    State.mError =
                        "ExternalProvider device source requires a registered provider before startup shader compilation.";
                    return false;
                }
                if (State.mExternalDeviceProvider->GetBackendType() !=
                    Configuration.mBackend)
                {
                    State.mError =
                        "The registered external device provider backend does not match the configured backend.";
                    return false;
                }
                const char* ProviderBackendName =
                    State.mExternalDeviceProvider->GetBackendName();
                if (ProviderBackendName && ProviderBackendName[0] &&
                    Configuration.mBackendName != ProviderBackendName)
                {
                    State.mError =
                        "The external device provider requires a different backend module.";
                    return false;
                }
            }
            return true;
        }

        bool CreateConfiguredDevice(
            FArdaBackendState& State,
            const FArdaBackendConfiguration& Configuration)
        {
            if (Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider)
            {
                if (!State.mExternalDeviceProvider)
                {
                    State.mError =
                        "ExternalProvider device source requires a registered provider.";
                    return false;
                }
                if (State.mExternalDeviceProvider->GetBackendType() !=
                    Configuration.mBackend)
                {
                    State.mError =
                        "The external device provider backend does not match the configuration.";
                    return false;
                }
                const char* ProviderBackendName =
                    State.mExternalDeviceProvider->GetBackendName();
                if (ProviderBackendName && ProviderBackendName[0] &&
                    Configuration.mBackendName != ProviderBackendName)
                {
                    State.mError =
                        "The external device provider requires a different backend module.";
                    return false;
                }
            }
            if (!State.mBackendModule)
            {
                State.mError = "No backend module was selected.";
                return false;
            }
            State.mBackendDevice = State.mBackendModule->CreateDevice(
                Configuration.mDeviceSource);

            if (!State.mBackendDevice)
            {
                State.mError = "The selected backend module failed to allocate a device implementation.";
                return false;
            }

            return true;
        }

        void PublishInitializedDevice(
            FArdaBackendState& State,
            const FArdaBackendConfiguration& Configuration)
        {
            State.mContext.mDevice = State.mBackendDevice->GetDevice();
            State.mContext.mBackendName = Configuration.mBackendName;
            State.mContext.mBackend = Configuration.mBackend;
            State.mContext.mDeviceSource = Configuration.mDeviceSource;
            State.mContext.mQueueCapabilities =
                State.mBackendDevice->GetQueueCapabilities();
            currentBackend = Configuration.mBackend;
            State.mbInitialized.store(true, std::memory_order_release);
            private_api::SetActiveBackendModule(State.mBackendModule);
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

        bool EnsureStartupShaders(
            FArdaBackendState& State,
            const FArdaBackendConfiguration& Configuration)
        {
            if (Configuration.mShaderCompilationMode !=
                EArdaShaderCompilationMode::Startup)
            {
                return true;
            }
            const FArdaShaderCompileResult Result =
                EnsureRegisteredShaderArtifacts(
                    Configuration.mShaderCacheDirectory,
                    Configuration.mBackend);
            if (Result)
                return true;
            State.mError = "Startup shader compilation failed";
            if (!Result.mDiagnostics.empty())
            {
                State.mError += ": ";
                State.mError += Result.mDiagnostics.front().mMessage;
            }
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

        private_api::RegisterLinkedBackendModules();
        FArdaBackendConfiguration selectedConfiguration = configuration;
        if (!configuration.mBackendName.empty())
        {
            IArdaBackendModule* Module = FindBackendModule(
                configuration.mBackendName.c_str());
            if (!Module)
            {
                state.mError = "The requested backend module is not registered.";
                return false;
            }
            selectedConfiguration.mBackend = Module->GetDescriptor().mBackendType;
        }

        if (configuration.mShaderCacheDirectory.empty())
        {
            state.mError = "The shader cache directory must not be empty.";
            return false;
        }
        std::error_code PathError;
        FArdaBackendConfiguration resolvedConfiguration = selectedConfiguration;
        resolvedConfiguration.mShaderCacheDirectory =
            std::filesystem::absolute(
                configuration.mShaderCacheDirectory,
                PathError).lexically_normal();
        if (PathError || resolvedConfiguration.mShaderCacheDirectory.empty())
        {
            state.mError =
                "The shader cache directory could not be resolved to an absolute path.";
            return false;
        }
        if (!configuration.mPipelineCacheDirectory.empty())
        {
            PathError.clear();
            resolvedConfiguration.mPipelineCacheDirectory =
                std::filesystem::absolute(
                    configuration.mPipelineCacheDirectory,
                    PathError).lexically_normal();
            if (PathError || resolvedConfiguration.mPipelineCacheDirectory.empty())
            {
                state.mError =
                    "The pipeline cache directory could not be resolved to an absolute path.";
                return false;
            }
            PathError.clear();
            const bool bPipelineCachePathExists = std::filesystem::exists(
                resolvedConfiguration.mPipelineCacheDirectory, PathError);
            if (PathError)
            {
                state.mError =
                    "The pipeline cache directory could not be inspected.";
                return false;
            }
            if (bPipelineCachePathExists &&
                !std::filesystem::is_directory(
                    resolvedConfiguration.mPipelineCacheDirectory, PathError))
            {
                state.mError =
                    "The pipeline cache path exists but is not a directory.";
                return false;
            }
            if (PathError)
            {
                state.mError =
                    "The pipeline cache directory could not be inspected.";
                return false;
            }
        }

        state.mConfiguration = resolvedConfiguration;
        state.mContext.mBackend = resolvedConfiguration.mBackend;
        state.mContext.mBackendName = resolvedConfiguration.mBackendName;
        state.mContext.mDeviceSource = configuration.mDeviceSource;
        currentBackend = resolvedConfiguration.mBackend;
        state.mError.clear();
        return true;
    }

    bool ConfigureBackend(EArdaBackendType backend)
    {
        auto configuration = GetBackendConfiguration();
        configuration.mBackend = backend;
        configuration.mBackendName.clear();
        return ConfigureBackend(configuration);
    }

    bool ConfigureBackend(const char* BackendName)
    {
        if (!BackendName || !BackendName[0])
        {
            SetBackendError("Backend module name must be non-empty.");
            return false;
        }
        auto Configuration = GetBackendConfiguration();
        Configuration.mBackendName = BackendName;
        return ConfigureBackend(Configuration);
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
        if (!ResolveAndValidateRuntimeConfiguration(state, runtimeConfiguration))
            return false;
        state.mConfiguration = runtimeConfiguration;
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
        if (!EnsureStartupShaders(state, runtimeConfiguration))
        {
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return false;
        }

        if (!CreateConfiguredDevice(state, runtimeConfiguration))
        {
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return false;
        }

        const IArdaExternalDeviceProvider* externalProvider =
            runtimeConfiguration.mDeviceSource == EArdaDeviceSource::ExternalProvider
            ? state.mExternalDeviceProvider
            : nullptr;
        if (state.mBackendDevice->Initialize(
                runtimeConfiguration, nullptr, externalProvider) !=
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
        if (!ResolveAndValidateRuntimeConfiguration(state, runtimeConfiguration))
            return EArdaInitializeResult::Failure;
        state.mConfiguration = runtimeConfiguration;
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
        if (!EnsureStartupShaders(state, runtimeConfiguration))
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

        const IArdaExternalDeviceProvider* externalProvider =
            runtimeConfiguration.mDeviceSource == EArdaDeviceSource::ExternalProvider
            ? state.mExternalDeviceProvider
            : nullptr;
        const EArdaInitializeResult result = state.mBackendDevice->Initialize(
            runtimeConfiguration, &WindowSurface, externalProvider);
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
            if (state.mContext.mDevice)
                state.mContext.mDevice->FlushAndDisablePipelineCachePersistence();
            else
                state.mBackendDevice->WaitForIdle();
        }
        state.mContext.mDevice = nullptr;
        state.mContext.mBackendName.clear();
        state.mContext.mQueueCapabilities = {};
        state.mContext.mDeviceSource = state.mConfiguration.mDeviceSource;
        state.mBackendDevice.reset();
        state.mBackendModule = nullptr;
        state.mbInitialized.store(false, std::memory_order_release);
        private_api::SetActiveBackendModule(nullptr);
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

    bool RegisterExternalDeviceProvider(IArdaExternalDeviceProvider& Provider)
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        if (state.mBackendDevice)
        {
            state.mError =
                "External device provider registration cannot change while initialized.";
            return false;
        }
        if (state.mExternalDeviceProvider &&
            state.mExternalDeviceProvider != &Provider)
        {
            state.mError = "A different external device provider is already registered.";
            return false;
        }
        state.mExternalDeviceProvider = &Provider;
        state.mError.clear();
        return true;
    }

    bool UnregisterExternalDeviceProvider(IArdaExternalDeviceProvider& Provider)
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        if (state.mBackendDevice)
        {
            state.mError =
                "External device provider registration cannot change while initialized.";
            return false;
        }
        if (state.mExternalDeviceProvider &&
            state.mExternalDeviceProvider != &Provider)
        {
            state.mError = "The specified external device provider is not registered.";
            return false;
        }
        state.mExternalDeviceProvider = nullptr;
        state.mError.clear();
        return true;
    }

    const IArdaExternalDeviceProvider* GetExternalDeviceProvider() noexcept
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        return state.mExternalDeviceProvider;
    }

    void SetBackendError(const char* Error)
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        state.mError = Error ? Error : "";
    }

    const char* ToString(EArdaBackendType backend) noexcept
    {
        switch (backend)
        {
        case EArdaBackendType::D3D12:
            return "D3D12";
        case EArdaBackendType::Vulkan:
            return "Vulkan";
        case EArdaBackendType::Custom:
            return "Custom";
        }
        return "Unknown";
    }

    const char* GetModuleName() noexcept
    {
        return "ArdaBackend";
    }
}
