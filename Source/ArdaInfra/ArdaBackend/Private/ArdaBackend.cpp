#include "ArdaBackendCorePch.h"

#include "ArdaBackend.h"
#include "ArdaBackendRegistry.h"
#include "ArdaExternalInterop.h"
#include "ArdaLinkedBackends.h"
#include "ArdaSwapChain.h"
#include "RHI/ArdaRHIDevicePrivate.h"
#include "ShaderStructs/ArdaShaderCompiler.h"
#include "ShaderStructs/ArdaShaderDirectoriesPrivate.h"

namespace arda
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
					ARDA_LOG(LogArdaBackend, Warning, "%s", messageText ? messageText : "");
					break;
				case EArdaDiagnosticSeverity::Error:
					ARDA_LOG(LogArdaBackend, Error, "%s", messageText ? messageText : "");
					break;
				case EArdaDiagnosticSeverity::Fatal:
					ARDA_LOG(LogArdaBackend, Fatal, "%s", messageText ? messageText : "");
					break;
				default:
					ARDA_LOG(LogArdaBackend, Log, "%s", messageText ? messageText : "");
					break;
				}
			}
		};

		struct FArdaOwnedBackendDevice
		{
			eastl::unique_ptr<IArdaBackendRuntime> mRuntime;
			FArdaBackendDevice mPublic;
		};

		struct FArdaBackendState
		{
			std::mutex mMutex;
			FArdaBackendConfiguration mConfiguration;
			FArdaDefaultMessageCallback mDefaultMessageCallback;
			eastl::vector<FArdaOwnedBackendDevice> mDevices;
			IArdaExternalDeviceProvider* mExternalDeviceProvider = nullptr;
			eastl::string mError;
			EArdaInitializeResult mInitializeResult = EArdaInitializeResult::Unavailable;
		};

		FArdaBackendState& GetState()
		{
			static FArdaBackendState state;
			return state;
		}

		bool ResolveConfiguration(FArdaBackendState& State,
		    FArdaBackendConfiguration& Configuration,
		    IArdaBackendModule*& OutModule,
		    bool bValidateRuntimeProvider)
		{
			arda::RegisterLinkedBackendModules();
			if (Configuration.mBackendName.empty() &&
			    Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider && State.mExternalDeviceProvider)
			{
				const char* ProviderBackendName = State.mExternalDeviceProvider->GetBackendName();
				if (ProviderBackendName && ProviderBackendName[0])
				{
					Configuration.mBackendName = ProviderBackendName;
				}
			}
			OutModule = Configuration.mBackendName.empty() ? FindDefaultBackendModule()
			                                               : FindBackendModule(Configuration.mBackendName.c_str());
			if (!OutModule)
			{
				State.mError = Configuration.mBackendName.empty()
				    ? "No linked backend module is registered in this build."
				    : "The configured backend module is not registered in this build.";
				return false;
			}
			const FArdaBackendModuleDescriptor& ModuleDescriptor = OutModule->GetDescriptor();
			Configuration.mBackendName = ModuleDescriptor.mName;
			const bool bExternal = Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider;
			if ((bExternal && !ModuleDescriptor.mbSupportsExternalDevice) ||
			    (!bExternal && !ModuleDescriptor.mbSupportsOwnedDevice))
			{
				State.mError = bExternal ? "The configured backend module cannot adopt external devices."
				                         : "The configured backend module cannot create an owned device.";
				return false;
			}
			// Validate selection shape before shader compilation or native device creation.
			const size_t DeviceCount = Configuration.mAdapters.empty() ? 1 : Configuration.mAdapters.size();
			if (Configuration.mDefaultDeviceIndex >= DeviceCount)
			{
				State.mError = "The default device index is outside the configured adapter list.";
				return false;
			}
			if (bExternal && !Configuration.mAdapters.empty())
			{
				State.mError = "ExternalProvider uses the host-selected device; the adapter list must be empty.";
				return false;
			}
			for (const auto& Adapter : Configuration.mAdapters)
			{
				if (Adapter.mBackendName != Configuration.mBackendName || Adapter.mValue.empty())
				{
					State.mError = "Each adapter must have a non-empty identity from the configured backend.";
					return false;
				}
			}

			if (Configuration.mShaderCacheDirectory.empty())
			{
				State.mError = "The shader cache directory must not be empty.";
				return false;
			}
			std::error_code Error;
			Configuration.mShaderCacheDirectory =
			    std::filesystem::absolute(Configuration.mShaderCacheDirectory, Error).lexically_normal();
			if (Error || Configuration.mShaderCacheDirectory.empty())
			{
				State.mError = "The shader cache directory could not be resolved to an absolute path.";
				return false;
			}
			if (!Configuration.mPipelineCacheDirectory.empty())
			{
				Error.clear();
				Configuration.mPipelineCacheDirectory =
				    std::filesystem::absolute(Configuration.mPipelineCacheDirectory, Error).lexically_normal();
				if (Error || Configuration.mPipelineCacheDirectory.empty())
				{
					State.mError = "The pipeline cache directory could not be resolved to an absolute path.";
					return false;
				}
				Error.clear();
				const bool Exists = std::filesystem::exists(Configuration.mPipelineCacheDirectory, Error);
				if (Error || (Exists && !std::filesystem::is_directory(Configuration.mPipelineCacheDirectory, Error)) ||
				    Error)
				{
					State.mError = "The pipeline cache path cannot be inspected or is not a directory.";
					return false;
				}
			}
			if (bValidateRuntimeProvider && Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider)
			{
				if (!State.mExternalDeviceProvider)
				{
					State.mError =
					    "ExternalProvider device source requires a registered provider before startup shader compilation.";
					return false;
				}
				const char* ProviderBackendName = State.mExternalDeviceProvider->GetBackendName();
				if (!ProviderBackendName || !ProviderBackendName[0])
				{
					State.mError = "The external device provider must identify an exact registered backend module.";
					return false;
				}
				if (Configuration.mBackendName != ProviderBackendName)
				{
					State.mError = "The external device provider requires a different backend module.";
					return false;
				}
			}
			return true;
		}

		EArdaInitializeResult CreateConfiguredDevices(FArdaBackendState& State,
		    const FArdaBackendConfiguration& Configuration,
		    IArdaBackendModule& Module,
		    IArdaWindowSurface* WindowSurface)
		{
			// Publish only after every requested device succeeds. Local ownership rolls back
			// earlier devices if a later adapter, feature requirement, or provider fails.
			eastl::vector<FArdaOwnedBackendDevice> Devices;
			const size_t DeviceCount = Configuration.mAdapters.empty() ? 1 : Configuration.mAdapters.size();
			const FArdaAdapterId Automatic;
			const IArdaExternalDeviceProvider* ExternalProvider =
			    Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider ? State.mExternalDeviceProvider
			                                                                       : nullptr;
			Devices.reserve(DeviceCount);
			for (size_t Index = 0; Index < DeviceCount; ++Index)
			{
				const auto& Adapter = Configuration.mAdapters.empty() ? Automatic : Configuration.mAdapters[Index];
				auto Result = Module.CreateDevice(Configuration,
				    Adapter,
				    Index == Configuration.mDefaultDeviceIndex ? WindowSurface : nullptr,
				    ExternalProvider);
				if (!Result)
				{
					State.mError = Result.mError.empty() ? "The selected backend module failed to create a device."
					                                     : eastl::move(Result.mError);
					return Result.mResult == EArdaInitializeResult::Success ? EArdaInitializeResult::Failure
					                                                        : Result.mResult;
				}

				// Enforce explicit selection even for custom providers: silently choosing a
				// different GPU would violate the caller's memory and feature assumptions.
				if (Result.mAdapter.mId.mBackendName != Configuration.mBackendName ||
				    Result.mAdapter.mId.mValue.empty() ||
				    (!Adapter.mValue.empty() && !(Result.mAdapter.mId == Adapter)))
				{
					State.mError = "The backend module did not return the requested adapter identity.";
					return EArdaInitializeResult::Failure;
				}
				FArdaOwnedBackendDevice Device;
				Device.mRuntime = eastl::move(Result.mBackendRuntime);
				Device.mPublic.mAdapter = eastl::move(Result.mAdapter);
				Device.mPublic.mDevice = CreateArdaRHIDevice(eastl::move(Result.mProviderDevice));
				if (!Device.mPublic.mDevice)
				{
					State.mError = "The backend module returned an invalid RHI provider device.";
					return EArdaInitializeResult::Failure;
				}
				Devices.push_back(eastl::move(Device));
			}
			State.mDevices = eastl::move(Devices);
			return EArdaInitializeResult::Success;
		}

		void PublishInitializedDevice(FArdaBackendState& State, IArdaBackendModule& Module)
		{
			arda::SetActiveBackendModule(&Module);
			State.mError.clear();
		}

		bool FreezeAndValidateShaderSources(FArdaBackendState& State)
		{
			const FArdaShaderDirectoryStatus DirectoryStatus = arda::ScanAndFreezeShaderSourceDirectoriesForBackend();
			if (!DirectoryStatus)
			{
				State.mError = eastl::string("Shader source directory registry failed: ") + DirectoryStatus.mMessage;
				return false;
			}
			const FArdaShaderRegistrationStatus RegistrationStatus = FArdaShaderTypeRegistration::CommitAll();
			if (!RegistrationStatus)
			{
				State.mError = eastl::string("Global shader registration failed: ") + RegistrationStatus.mMessage;
				return false;
			}
			return true;
		}

		bool BeginShaderDirectoryUse(FArdaBackendState& State)
		{
			const FArdaShaderDirectoryStatus Status = arda::BeginShaderDirectoryRegistryUse();
			if (Status)
			{
				return true;
			}
			State.mError = eastl::string("Shader source directory registry is unavailable: ") + Status.mMessage;
			return false;
		}

		bool EnsureStartupShaders(FArdaBackendState& State, const FArdaBackendConfiguration& Configuration)
		{
			if (Configuration.mShaderCompilationMode != EArdaShaderCompilationMode::Startup)
			{
				return true;
			}
			const FArdaShaderCompileResult Result = EnsureRegisteredShaderArtifacts(Configuration.mShaderCacheDirectory,
			    Configuration.mBackendName.c_str());
			if (Result)
			{
				return true;
			}
			State.mError = "Startup shader compilation failed";
			if (!Result.mDiagnostics.empty())
			{
				State.mError += ": ";
				State.mError += Result.mDiagnostics.front().mMessage;
			}
			return false;
		}

		bool PrepareInitialization(FArdaBackendState& State,
		    FArdaBackendConfiguration& RuntimeConfiguration,
		    IArdaBackendModule*& OutModule)
		{
			if (!ResolveConfiguration(State, RuntimeConfiguration, OutModule, true))
			{
				return false;
			}
			State.mConfiguration = RuntimeConfiguration;
			if (!RuntimeConfiguration.mMessageCallback)
			{
				RuntimeConfiguration.mMessageCallback = &State.mDefaultMessageCallback;
			}
			if (!BeginShaderDirectoryUse(State))
			{
				return false;
			}
			if (!FreezeAndValidateShaderSources(State) || !EnsureStartupShaders(State, RuntimeConfiguration))
			{
				arda::CompleteShaderDirectoryRegistryUse(false);
				return false;
			}
			return true;
		}
	}

	bool ConfigureBackend(const FArdaBackendConfiguration& configuration)
	{
		auto& state = GetState();
		std::lock_guard<std::mutex> lock(state.mMutex);
		if (!state.mDevices.empty())
		{
			state.mError = "The backend cannot be reconfigured after initialization.";
			return false;
		}

		if (configuration.mCudaExecutionMode > EArdaCudaExecutionMode::ContextSwitch)
		{
			state.mError = "Invalid CUDA execution mode.";
			return false;
		}

		FArdaBackendConfiguration resolvedConfiguration = configuration;
		IArdaBackendModule* Module = nullptr;
		if (!ResolveConfiguration(state, resolvedConfiguration, Module, false))
		{
			return false;
		}
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

		// Clear a prior missing-layer outcome before any shader/configuration gate.
		// Tests may skip absent validation, but a later unrelated failure must still fail.
		state.mInitializeResult = EArdaInitializeResult::Failure;
		if (!state.mDevices.empty())
		{
			state.mInitializeResult = EArdaInitializeResult::Success;
			return true;
		}

		FArdaBackendConfiguration runtimeConfiguration = state.mConfiguration;
		IArdaBackendModule* Module = nullptr;
		if (!PrepareInitialization(state, runtimeConfiguration, Module))
		{
			return false;
		}
		state.mInitializeResult = CreateConfiguredDevices(state, runtimeConfiguration, *Module, nullptr);
		if (state.mInitializeResult != EArdaInitializeResult::Success)
		{
			arda::CompleteShaderDirectoryRegistryUse(false);
			return false;
		}

		PublishInitializedDevice(state, *Module);
		arda::CompleteShaderDirectoryRegistryUse(true);
		return true;
	}

	EArdaInitializeResult GetBackendInitializeResult() noexcept
	{
		auto& State = GetState();
		std::lock_guard<std::mutex> Lock(State.mMutex);
		return State.mInitializeResult;
	}

	EArdaInitializeResult InitializeBackendForPresentation(IArdaWindowSurface& WindowSurface,
	    uint32_t Width,
	    uint32_t Height,
	    eastl::unique_ptr<IArdaSwapChain>& OutSwapChain)
	{
		OutSwapChain.reset();

		auto& state = GetState();
		std::lock_guard<std::mutex> lock(state.mMutex);
		if (!state.mDevices.empty())
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
		{
			return EArdaInitializeResult::Failure;
		}
		const EArdaInitializeResult result =
		    CreateConfiguredDevices(state, runtimeConfiguration, *Module, &WindowSurface);
		if (result != EArdaInitializeResult::Success)
		{
			arda::CompleteShaderDirectoryRegistryUse(false);
			return result;
		}

		auto& PresentationDevice = state.mDevices[state.mConfiguration.mDefaultDeviceIndex];
		FArdaSwapChainCreateResult SwapChainResult =
		    PresentationDevice.mRuntime->CreateSwapChain(Width, Height, PresentationDevice.mPublic.mDevice);
		if (!SwapChainResult)
		{
			state.mError = eastl::move(SwapChainResult.mError);
			state.mDevices.clear();
			arda::CompleteShaderDirectoryRegistryUse(false);
			return EArdaInitializeResult::Failure;
		}
		OutSwapChain = eastl::move(SwapChainResult.mSwapChain);

		PublishInitializedDevice(state, *Module);
		arda::CompleteShaderDirectoryRegistryUse(true);
		return EArdaInitializeResult::Success;
	}

	void ShutdownBackend() noexcept
	{
		auto& state = GetState();
		std::lock_guard<std::mutex> lock(state.mMutex);
		for (auto& Device : state.mDevices)
		{
			Device.mPublic.mDevice->FlushAndDisablePipelineCachePersistence();
		}
		state.mDevices.clear();
		arda::SetActiveBackendModule(nullptr);
		arda::ReleaseShaderDirectoryRegistryAfterShutdown();
	}

	bool IsBackendInitialized() noexcept
	{
		return !GetState().mDevices.empty();
	}

	arda::FArdaRHIDeviceRef GetDevice() noexcept
	{
		return GetDevice(GetState().mConfiguration.mDefaultDeviceIndex);
	}

	FArdaRHIDeviceRef GetDevice(uint32_t DeviceIndex) noexcept
	{
		const auto& Devices = GetState().mDevices;
		return DeviceIndex < Devices.size() ? Devices[DeviceIndex].mPublic.mDevice : FArdaRHIDeviceRef{};
	}

	eastl::vector<FArdaBackendDevice> GetDevices()
	{
		auto& State = GetState();
		std::lock_guard<std::mutex> Lock(State.mMutex);
		eastl::vector<FArdaBackendDevice> Devices;
		Devices.reserve(State.mDevices.size());
		for (const auto& Device : State.mDevices)
		{
			Devices.push_back(Device.mPublic);
		}
		return Devices;
	}

	bool SetDefaultDevice(uint32_t DeviceIndex)
	{
		auto& State = GetState();
		std::lock_guard<std::mutex> Lock(State.mMutex);
		if (DeviceIndex >= State.mDevices.size())
		{
			State.mError = "The default device index must name an initialized device.";
			return false;
		}
		State.mConfiguration.mDefaultDeviceIndex = DeviceIndex;
		State.mError.clear();
		return true;
	}

	TArdaRHIResult<eastl::vector<FArdaAdapterInfo>> EnumerateAdapters(const char* BackendName)
	{
		auto& State = GetState();
		std::lock_guard<std::mutex> Lock(State.mMutex);
		RegisterLinkedBackendModules();
		const char* Name = BackendName ? BackendName : State.mConfiguration.mBackendName.c_str();
		const auto* Module = Name[0] ? FindBackendModule(Name) : FindDefaultBackendModule();
		if (!Module)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			        "The requested backend module is not registered.")};
		}
		return Module->EnumerateAdapters();
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
		if (!state.mDevices.empty())
		{
			state.mError = "External device provider registration cannot change while initialized.";
			return false;
		}
		if (state.mExternalDeviceProvider && state.mExternalDeviceProvider != &Provider)
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
		if (!state.mDevices.empty())
		{
			state.mError = "External device provider registration cannot change while initialized.";
			return false;
		}
		if (state.mExternalDeviceProvider && state.mExternalDeviceProvider != &Provider)
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

	const char* GetBackendModuleName() noexcept
	{
		return "ArdaBackend";
	}
}
