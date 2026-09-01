#include "ArdaBackendCorePch.h"

#include "ArdaBackend.h"
#include "ArdaBackendRegistry.h"
#include "ArdaExternalInterop.h"
#include "ArdaLinkedBackends.h"
#include "ArdaSwapChain.h"
#include "RHI/ArdaRHIDevicePrivate.h"
#include "ShaderStructs/ArdaShaderCompiler.h"
#include "ShaderStructs/ArdaShaderDirectoriesPrivate.h"

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

        struct FArdaBackendState
        {
            std::mutex mMutex;
            FArdaBackendConfiguration mConfiguration;
            FArdaDefaultMessageCallback mDefaultMessageCallback;
            eastl::unique_ptr<IArdaBackendRuntime> mBackendRuntime;
            rhi::FArdaRHIDeviceRef mDevice;
            IArdaExternalDeviceProvider* mExternalDeviceProvider = nullptr;
            eastl::string mError;
        };

        FArdaBackendState& GetState()
        {
            static FArdaBackendState state;
            return state;
        }

        bool ResolveConfiguration(
            FArdaBackendState& State,
            FArdaBackendConfiguration& Configuration,
            IArdaBackendModule*& OutModule,
            bool bValidateRuntimeProvider)
        {
            private_api::RegisterLinkedBackendModules();
            if (Configuration.mBackendName.empty() &&
                Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider &&
                State.mExternalDeviceProvider)
            {
                const char* ProviderBackendName =
                    State.mExternalDeviceProvider->GetBackendName();
                if (ProviderBackendName && ProviderBackendName[0])
                    Configuration.mBackendName = ProviderBackendName;
            }
            OutModule = Configuration.mBackendName.empty()
                ? FindDefaultBackendModule()
                : FindBackendModule(Configuration.mBackendName.c_str());
            if (!OutModule)
            {
                State.mError = Configuration.mBackendName.empty()
                    ? "No linked backend module is registered in this build."
                    : "The configured backend module is not registered in this build.";
                return false;
            }
            const FArdaBackendModuleDescriptor& ModuleDescriptor =
                OutModule->GetDescriptor();
            Configuration.mBackendName = ModuleDescriptor.mName;
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
            if (bValidateRuntimeProvider &&
                Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider)
            {
                if (!State.mExternalDeviceProvider)
                {
                    State.mError =
                        "ExternalProvider device source requires a registered provider before startup shader compilation.";
                    return false;
                }
                const char* ProviderBackendName =
                    State.mExternalDeviceProvider->GetBackendName();
                if (!ProviderBackendName || !ProviderBackendName[0])
                {
                    State.mError =
                        "The external device provider must identify an exact registered backend module.";
                    return false;
                }
                if (Configuration.mBackendName != ProviderBackendName)
                {
                    State.mError =
                        "The external device provider requires a different backend module.";
                    return false;
                }
            }
            return true;
        }

        EArdaInitializeResult CreateConfiguredDevice(
            FArdaBackendState& State,
            const FArdaBackendConfiguration& Configuration,
            IArdaBackendModule& Module,
            IArdaWindowSurface* WindowSurface)
        {
            const IArdaExternalDeviceProvider* ExternalProvider =
                Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider
                ? State.mExternalDeviceProvider
                : nullptr;
            FArdaBackendDeviceCreateResult Result = Module.CreateDevice(
                Configuration, WindowSurface, ExternalProvider);
            if (!Result)
            {
                State.mError = eastl::move(Result.mError);
                if (State.mError.empty())
                    State.mError = "The selected backend module failed to create a device.";
                return Result.mResult;
            }
            State.mDevice = rhi::provider::CreateArdaRHIDevice(
                eastl::move(Result.mProviderDevice));
            if (!State.mDevice)
            {
                State.mError =
                    "The backend module returned an invalid RHI provider device.";
                return EArdaInitializeResult::Failure;
            }
            State.mBackendRuntime = eastl::move(Result.mBackendRuntime);
            return EArdaInitializeResult::Success;
        }

        void PublishInitializedDevice(
            FArdaBackendState& State,
            IArdaBackendModule& Module)
        {
            private_api::SetActiveBackendModule(&Module);
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
                    Configuration.mBackendName.c_str());
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

        bool PrepareInitialization(
            FArdaBackendState& State,
            FArdaBackendConfiguration& RuntimeConfiguration,
            IArdaBackendModule*& OutModule)
        {
            if (!ResolveConfiguration(
                    State, RuntimeConfiguration, OutModule, true))
                return false;
            State.mConfiguration = RuntimeConfiguration;
            if (!RuntimeConfiguration.mMessageCallback)
                RuntimeConfiguration.mMessageCallback = &State.mDefaultMessageCallback;
            if (!BeginShaderDirectoryUse(State))
                return false;
            if (!FreezeAndValidateShaderSources(State) ||
                !EnsureStartupShaders(State, RuntimeConfiguration))
            {
                private_api::CompleteShaderDirectoryRegistryUse(false);
                return false;
            }
            return true;
        }
    }

    bool ConfigureBackend(const FArdaBackendConfiguration& configuration)
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        if (state.mBackendRuntime)
        {
            state.mError = "The backend cannot be reconfigured after initialization.";
            return false;
        }

        FArdaBackendConfiguration resolvedConfiguration = configuration;
        IArdaBackendModule* Module = nullptr;
        if (!ResolveConfiguration(
                state, resolvedConfiguration, Module, false))
            return false;
        state.mConfiguration = resolvedConfiguration;
        state.mError.clear();
        return true;
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
        if (state.mBackendRuntime)
            return state.mDevice != nullptr;

        FArdaBackendConfiguration runtimeConfiguration = state.mConfiguration;
        IArdaBackendModule* Module = nullptr;
        if (!PrepareInitialization(state, runtimeConfiguration, Module))
            return false;
        if (CreateConfiguredDevice(
                state, runtimeConfiguration, *Module, nullptr) !=
            EArdaInitializeResult::Success)
        {
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return false;
        }

        PublishInitializedDevice(state, *Module);
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
        if (state.mBackendRuntime)
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
        IArdaBackendModule* Module = nullptr;
        if (!PrepareInitialization(state, runtimeConfiguration, Module))
            return EArdaInitializeResult::Failure;
        const EArdaInitializeResult result = CreateConfiguredDevice(
            state, runtimeConfiguration, *Module, &WindowSurface);
        if (result != EArdaInitializeResult::Success)
        {
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return result;
        }

        FArdaSwapChainCreateResult SwapChainResult =
            state.mBackendRuntime->CreateSwapChain(
                Width, Height, state.mDevice);
        if (!SwapChainResult)
        {
            state.mError = eastl::move(SwapChainResult.mError);
            state.mDevice = nullptr;
            state.mBackendRuntime.reset();
            private_api::CompleteShaderDirectoryRegistryUse(false);
            return EArdaInitializeResult::Failure;
        }
        OutSwapChain = eastl::move(SwapChainResult.mSwapChain);

        PublishInitializedDevice(state, *Module);
        private_api::CompleteShaderDirectoryRegistryUse(true);
        return EArdaInitializeResult::Success;
    }

    void ShutdownBackend() noexcept
    {
        auto& state = GetState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        if (state.mDevice)
            state.mDevice->FlushAndDisablePipelineCachePersistence();
        state.mDevice = nullptr;
        state.mBackendRuntime.reset();
        private_api::SetActiveBackendModule(nullptr);
        private_api::ReleaseShaderDirectoryRegistryAfterShutdown();
    }

    bool IsBackendInitialized() noexcept
    {
        return GetState().mDevice != nullptr;
    }

    rhi::FArdaRHIDeviceRef GetDevice() noexcept
    {
        return GetState().mDevice;
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
        if (state.mBackendRuntime)
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
        if (state.mBackendRuntime)
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

    const char* GetModuleName() noexcept
    {
        return "ArdaBackend";
    }
}
