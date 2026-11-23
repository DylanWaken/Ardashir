#include "ArdaBackend.h"
#include "RHI/Config/ArdaDefaultMessageCallback.h"
#include "RHI/Context/ArdaExternalResourceContext.h"
#include "RHI/Device/ArdaRHIDevicePrivate.h"
#include "RHI/Shaders/ArdaShaderDirectoriesPrivate.h"

#include <EASTL/algorithm.h>
#include <exception>
#include <mutex>

namespace arda
{
#define ARDA_LINKED_BACKEND(Function) [[nodiscard]] bool Function();
#include "ArdaLinkedBackendRegistrations.inl"
#undef ARDA_LINKED_BACKEND

	namespace
	{
		struct FArdaOwnedBackendDevice
		{
			// Members are destroyed in reverse order: release the RHI facade before its runtime.
			eastl::unique_ptr<IArdaBackendRuntime> mRuntime;
			FArdaBackendDeviceReg mPublic;
		};

		// The descriptor is immutable while registered. Store a snapshot so lookup never calls host code under a lock.
		struct FArdaRegisteredBackendModule
		{
			FArdaBackendModuleDescriptor mDescriptor;
			IArdaBackendModule* mModule;
		};

		struct FArdaBackendState
		{
			std::mutex mLifecycleMutex;
			FArdaBackendConfiguration mConfiguration;
			FArdaDefaultMessageCallback mDefaultMessageCallback;
			IArdaExternalDeviceProvider* mExternalDeviceProvider = nullptr;
			eastl::string mError;
			EArdaInitializeResult mInitializeResult = EArdaInitializeResult::Unavailable;

			// Device snapshots and registry lookup must remain available inside provider callbacks.
			std::mutex mRegistryMutex;
			std::once_flag mLinkedRegistration;
			eastl::vector<FArdaRegisteredBackendModule> mModules;
			IArdaBackendModule* mModuleInUse = nullptr;
			IArdaBackendModule* mActiveModule = nullptr;
			eastl::vector<FArdaOwnedBackendDevice> mDevices;
			FArdaResourceProviderRegistry mResourceProviders;
		};

		FArdaBackendState& BackendState()
		{
			static FArdaBackendState State;
			return State;
		}

		void RegisterLinkedBackendModules()
		{
			std::call_once(BackendState().mLinkedRegistration,
			    []
			    {
#define ARDA_LINKED_BACKEND(Function) (void)Function();
#include "ArdaLinkedBackendRegistrations.inl"
#undef ARDA_LINKED_BACKEND
			    });
		}

		const FArdaRegisteredBackendModule* FindModuleLocked(const FArdaBackendState& State, const char* Name)
		{
			const FArdaRegisteredBackendModule* Best = nullptr;
			for (const auto& Entry : State.mModules)
			{
				if (Name && Entry.mDescriptor.mName == Name)
				{
					return &Entry;
				}
				if (!Name && (!Best || Entry.mDescriptor.mPriority > Best->mDescriptor.mPriority))
				{
					Best = &Entry;
				}
			}
			return Best;
		}

		// A lifecycle operation holds mLifecycleMutex and borrows one host-owned module.
		// Pin its registration while calling it without holding the registry lock.
		class FArdaModuleUse
		{
		public:
			FArdaModuleUse(FArdaBackendState& State, const char* Name)
			    : mState(State)
			{
				RegisterLinkedBackendModules();
				std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
				const auto* Entry = FindModuleLocked(State, Name);
				State.mModuleInUse = Entry ? Entry->mModule : nullptr;
			}

			~FArdaModuleUse()
			{
				std::lock_guard<std::mutex> Lock(mState.mRegistryMutex);
				mState.mModuleInUse = nullptr;
			}

			IArdaBackendModule* Get() const
			{
				return mState.mModuleInUse;
			}

			FArdaModuleUse(const FArdaModuleUse&) = delete;
			FArdaModuleUse& operator=(const FArdaModuleUse&) = delete;

		private:
			FArdaBackendState& mState;
		};

		const char* RequestedModuleName(const FArdaBackendState& State, FArdaBackendConfiguration& Configuration)
		{
			if (!Configuration.mBackendName.empty())
			{
				return Configuration.mBackendName.c_str();
			}
			if (Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider && State.mExternalDeviceProvider)
			{
				const char* Name = State.mExternalDeviceProvider->GetBackendName();
				if (Name && *Name)
				{
					Configuration.mBackendName = Name;
					return Configuration.mBackendName.c_str();
				}
			}
			return nullptr;
		}

		bool IsDescriptorValid(const FArdaBackendModuleDescriptor& Descriptor)
		{
			return Descriptor.mInterfaceVersion == ArdaBackendProviderInterfaceVersion && !Descriptor.mName.empty() &&
			    !Descriptor.mShaderArtifactExtension.empty() && Descriptor.mShaderArtifactExtension.front() == '.';
		}

		bool CopyShaderTarget(const FArdaRegisteredBackendModule* Module, FArdaShaderTarget& OutTarget) noexcept
		{
			OutTarget = {};
			if (!Module)
			{
				return false;
			}
			const auto& Descriptor = Module->mDescriptor;
			OutTarget.mBackendName = Descriptor.mName;
			OutTarget.mBinaryFormat = Descriptor.mShaderBinaryFormat;
			OutTarget.mArtifactExtension = Descriptor.mShaderArtifactExtension;
			OutTarget.mCompilerIdentity = Descriptor.mShaderCompilerIdentity;
			return static_cast<bool>(OutTarget);
		}

		bool ResolveConfiguration(FArdaBackendState& State,
		    FArdaBackendConfiguration& Configuration,
		    IArdaBackendModule* Module,
		    bool bValidateRuntimeProvider)
		{
			if (!Module)
			{
				State.mError = Configuration.mBackendName.empty()
				    ? "No linked backend module is registered in this build."
				    : "The configured backend module is not registered in this build.";
				return false;
			}
			const FArdaBackendModuleDescriptor& ModuleDescriptor = Module->GetDescriptor();
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
			for (const auto& Feature : Configuration.mRequiredFeatures.GetEntries())
			{
				if (!Feature.IsValid())
				{
					State.mError = eastl::string("Invalid Arda feature requirement: ") + Feature.mName;
					return false;
				}
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

		bool ConfigureLocked(FArdaBackendState& State, FArdaBackendConfiguration Configuration)
		{
			if (!State.mDevices.empty())
			{
				State.mError = "The backend cannot be reconfigured after initialization.";
				return false;
			}

			if (Configuration.mCudaExecutionMode > EArdaCudaExecutionMode::ContextSwitch)
			{
				State.mError = "Invalid CUDA execution mode.";
				return false;
			}

			FArdaModuleUse Module(State, RequestedModuleName(State, Configuration));
			if (!ResolveConfiguration(State, Configuration, Module.Get(), false))
			{
				return false;
			}
			State.mConfiguration = eastl::move(Configuration);
			State.mError.clear();
			return true;
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

		EArdaInitializeResult CreateConfiguredDevices(FArdaBackendState& State,
		    const FArdaBackendConfiguration& Configuration,
		    IArdaBackendModule& Module,
		    IArdaWindowSurface* WindowSurface,
		    eastl::vector<FArdaOwnedBackendDevice>& Devices)
		{
			// Publish only after every requested device succeeds. Local ownership rolls back
			// earlier devices if a later adapter, feature requirement, or provider fails.
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
				Device.mPublic.mDevice =
				    CreateArdaRHIDevice(eastl::move(Result.mProviderDevice), Configuration.mBackendName);
				if (!Device.mPublic.mDevice)
				{
					State.mError = "The backend module returned an invalid RHI provider device.";
					return EArdaInitializeResult::Failure;
				}
				Device.mPublic.mFeatures = Device.mPublic.mDevice->GetFeatures();
				if (auto Report = Configuration.EvaluateDevice(*Device.mPublic.mFeatures); !Report.IsSupported())
				{
					State.mError = Report.ToStatus().mMessage;
					return EArdaInitializeResult::Unavailable;
				}
				if (auto Status = GetArdaNativeComputeModuleStatus(); !Status)
				{
					State.mError = Status.mMessage;
					return EArdaInitializeResult::Failure;
				}
				GetArdaNativeComputeRegistry().ProbeDeviceProviders(Device.mPublic.mDevice);
				Devices.push_back(eastl::move(Device));
			}
			return EArdaInitializeResult::Success;
		}

		struct FArdaShaderDirectoryUse
		{
			bool mbInitialized = false;

			~FArdaShaderDirectoryUse()
			{
				CompleteShaderDirectoryRegistryUse(mbInitialized);
			}
		};

		EArdaInitializeResult InitializeLocked(FArdaBackendState& State,
		    IArdaWindowSurface* Surface,
		    uint32_t Width,
		    uint32_t Height,
		    eastl::unique_ptr<IArdaSwapChain>* OutSwapChain)
		try
		{
			auto Configuration = State.mConfiguration;
			FArdaModuleUse Module(State, RequestedModuleName(State, Configuration));
			if (!ResolveConfiguration(State, Configuration, Module.Get(), true))
			{
				return EArdaInitializeResult::Failure;
			}
			State.mConfiguration = Configuration;
			if (!Configuration.mMessageCallback)
			{
				Configuration.mMessageCallback = &State.mDefaultMessageCallback;
			}
			const auto DirectoryStatus = BeginShaderDirectoryRegistryUse();
			if (!DirectoryStatus)
			{
				State.mError =
				    eastl::string("Shader source directory registry is unavailable: ") + DirectoryStatus.mMessage;
				return EArdaInitializeResult::Failure;
			}
			FArdaShaderDirectoryUse DirectoryUse;
			if (!FreezeAndValidateShaderSources(State) || !EnsureStartupShaders(State, Configuration))
			{
				return EArdaInitializeResult::Failure;
			}
			eastl::vector<FArdaOwnedBackendDevice> Devices;
			auto Result = CreateConfiguredDevices(State, Configuration, *Module.Get(), Surface, Devices);
			if (Result != EArdaInitializeResult::Success)
			{
				return Result;
			}
			if (OutSwapChain)
			{
				auto& Device = Devices[Configuration.mDefaultDeviceIndex];
				auto SwapChain = Device.mRuntime->CreateSwapChain(Width, Height, Device.mPublic.mDevice);
				if (!SwapChain)
				{
					State.mError = eastl::move(SwapChain.mError);
					return EArdaInitializeResult::Failure;
				}
				*OutSwapChain = eastl::move(SwapChain.mSwapChain);
			}
			{
				std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
				State.mDevices.swap(Devices);
				State.mActiveModule = Module.Get();
			}
			State.mError.clear();
			DirectoryUse.mbInitialized = true;
			return EArdaInitializeResult::Success;
		}
		catch (const std::exception& Error)
		{
			State.mError = Error.what();
			return EArdaInitializeResult::Failure;
		}
		catch (...)
		{
			State.mError = "Backend initialization threw an exception.";
			return EArdaInitializeResult::Failure;
		}

	}

	bool ConfigureBackend(const FArdaBackendConfiguration& Configuration)
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		return ConfigureLocked(State, Configuration);
	}

	bool ConfigureBackend(const char* BackendName)
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		if (!BackendName || !*BackendName)
		{
			State.mError = "Backend module name must be non-empty.";
			return false;
		}
		auto Configuration = State.mConfiguration;
		Configuration.mBackendName = BackendName;
		return ConfigureLocked(State, eastl::move(Configuration));
	}

	const FArdaBackendConfiguration& GetBackendConfiguration() noexcept
	{
		return BackendState().mConfiguration;
	}

	bool InitializeBackend()
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		State.mInitializeResult =
		    State.mDevices.empty() ? InitializeLocked(State, nullptr, 0, 0, nullptr) : EArdaInitializeResult::Success;
		return State.mInitializeResult == EArdaInitializeResult::Success;
	}

	EArdaInitializeResult GetBackendInitializeResult() noexcept
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		return State.mInitializeResult;
	}

	EArdaInitializeResult InitializeBackendForPresentation(IArdaWindowSurface& WindowSurface,
	    uint32_t Width,
	    uint32_t Height,
	    eastl::unique_ptr<IArdaSwapChain>& OutSwapChain)
	{
		// On success the previous caller-owned chain retires after releasing the lifecycle lock.
		eastl::unique_ptr<IArdaSwapChain> SwapChain;
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		if (!State.mDevices.empty() || !Width || !Height)
		{
			State.mError = !State.mDevices.empty() ? "The backend is already initialized."
			                                       : "Presentation dimensions must be non-zero.";
			return EArdaInitializeResult::Failure;
		}
		const auto Result = InitializeLocked(State, &WindowSurface, Width, Height, &SwapChain);
		if (Result == EArdaInitializeResult::Success)
		{
			SwapChain.swap(OutSwapChain);
		}
		return Result;
	}

	void ShutdownBackend() noexcept
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		for (auto& Device : State.mDevices)
		{
			Device.mPublic.mDevice->FlushAndDisablePipelineCachePersistence();
		}
		eastl::vector<FArdaOwnedBackendDevice> Devices;
		{
			std::lock_guard<std::mutex> RegistryLock(State.mRegistryMutex);
			Devices.swap(State.mDevices);
			State.mModuleInUse = State.mActiveModule;
			State.mActiveModule = nullptr;
		}
		// Retire the device and module together. Provider destructors may query the registry.
		Devices.clear();
		{
			std::lock_guard<std::mutex> RegistryLock(State.mRegistryMutex);
			State.mModuleInUse = nullptr;
		}
		ReleaseShaderDirectoryRegistryAfterShutdown();
	}

	bool IsBackendInitialized() noexcept
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		return !State.mDevices.empty();
	}

	FArdaRHIDeviceRef GetDevice() noexcept
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		return State.mDevices.empty() ? FArdaRHIDeviceRef{}
		                              : State.mDevices[State.mConfiguration.mDefaultDeviceIndex].mPublic.mDevice;
	}

	FArdaRHIDeviceRef GetDevice(uint32_t DeviceIndex) noexcept
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		return DeviceIndex < State.mDevices.size() ? State.mDevices[DeviceIndex].mPublic.mDevice : FArdaRHIDeviceRef{};
	}

	FArdaBackendFeatureListRef GetBackendFeatures() noexcept
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		return State.mDevices.empty() ? nullptr : State.mDevices[State.mConfiguration.mDefaultDeviceIndex].mPublic.mFeatures;
	}

	FArdaBackendFeatureListRef GetBackendFeatures(uint32_t DeviceIndex) noexcept
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		return DeviceIndex < State.mDevices.size() ? State.mDevices[DeviceIndex].mPublic.mFeatures : nullptr;
	}

	eastl::vector<FArdaBackendDeviceReg> GetDevices()
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		eastl::vector<FArdaBackendDeviceReg> Devices;
		Devices.reserve(State.mDevices.size());
		for (const auto& Device : State.mDevices)
		{
			Devices.push_back(Device.mPublic);
		}
		return Devices;
	}

	bool SetDefaultDevice(uint32_t DeviceIndex)
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> LifecycleLock(State.mLifecycleMutex);
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
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
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		const char* Name = BackendName ? BackendName : State.mConfiguration.mBackendName.c_str();
		FArdaModuleUse Module(State, *Name ? Name : nullptr);
		if (!Module.Get())
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			        "The requested backend module is not registered.")};
		}
		return Module.Get()->EnumerateAdapters();
	}

	eastl::string GetBackendError()
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		return State.mError;
	}

	const char* GetBackendModuleName() noexcept
	{
		return "ArdaBackend";
	}

	bool RegisterBackendModule(IArdaBackendModule& Module)
	{
		const FArdaBackendModuleDescriptor& Descriptor = Module.GetDescriptor();
		if (!IsDescriptorValid(Descriptor))
		{
			return false;
		}

		auto& Registry = BackendState();
		std::lock_guard<std::mutex> Lock(Registry.mRegistryMutex);
		auto Position = eastl::lower_bound(Registry.mModules.begin(),
		    Registry.mModules.end(),
		    Descriptor.mName,
		    [](const FArdaRegisteredBackendModule& Entry, const eastl::string& Name)
		    {
			    return Entry.mDescriptor.mName < Name;
		    });
		if (Position != Registry.mModules.end() && Position->mDescriptor.mName == Descriptor.mName)
		{
			return Position->mModule == &Module;
		}
		Registry.mModules.insert(Position, FArdaRegisteredBackendModule{Descriptor, &Module});
		return true;
	}

	bool UnregisterBackendModule(IArdaBackendModule& Module)
	{
		auto& Registry = BackendState();
		std::lock_guard<std::mutex> Lock(Registry.mRegistryMutex);
		if (Registry.mActiveModule == &Module || Registry.mModuleInUse == &Module)
		{
			return false;
		}
		for (auto It = Registry.mModules.begin(); It != Registry.mModules.end(); ++It)
		{
			if (It->mModule == &Module)
			{
				Registry.mModules.erase(It);
				return true;
			}
		}
		return true;
	}

	IArdaBackendModule* FindBackendModule(const char* Name) noexcept
	{
		RegisterLinkedBackendModules();
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		const auto* Entry = Name ? FindModuleLocked(State, Name) : nullptr;
		return Entry ? Entry->mModule : nullptr;
	}

	IArdaBackendModule* FindDefaultBackendModule() noexcept
	{
		RegisterLinkedBackendModules();
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		const auto* Entry = FindModuleLocked(State, nullptr);
		return Entry ? Entry->mModule : nullptr;
	}

	bool ResolveShaderTarget(const char* BackendName, FArdaShaderTarget& OutTarget) noexcept
	{
		RegisterLinkedBackendModules();
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		return CopyShaderTarget(BackendName ? FindModuleLocked(State, BackendName) : nullptr, OutTarget);
	}

	bool ResolveDefaultShaderTarget(FArdaShaderTarget& OutTarget) noexcept
	{
		RegisterLinkedBackendModules();
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		return CopyShaderTarget(FindModuleLocked(State, nullptr), OutTarget);
	}

	eastl::vector<FArdaBackendModuleDescriptor> EnumerateBackendModules()
	{
		RegisterLinkedBackendModules();
		auto& Registry = BackendState();
		std::lock_guard<std::mutex> Lock(Registry.mRegistryMutex);
		eastl::vector<FArdaBackendModuleDescriptor> Result;
		Result.reserve(Registry.mModules.size());
		for (const auto& Entry : Registry.mModules)
		{
			Result.push_back(Entry.mDescriptor);
		}
		return Result;
	}

	const IArdaBackendModule* GetActiveBackendModule() noexcept
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mRegistryMutex);
		return State.mActiveModule;
	}

	FArdaResourceProviderRegistry& GetResourceProviderRegistry()
	{
		return BackendState().mResourceProviders;
	}

	bool RegisterExternalDeviceProvider(IArdaExternalDeviceProvider& Provider)
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		if (!State.mDevices.empty())
		{
			State.mError = "External device provider registration cannot change while initialized.";
			return false;
		}
		if (State.mExternalDeviceProvider && State.mExternalDeviceProvider != &Provider)
		{
			State.mError = "A different external device provider is already registered.";
			return false;
		}
		State.mExternalDeviceProvider = &Provider;
		State.mError.clear();
		return true;
	}

	bool UnregisterExternalDeviceProvider(IArdaExternalDeviceProvider& Provider)
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		if (!State.mDevices.empty())
		{
			State.mError = "External device provider registration cannot change while initialized.";
			return false;
		}
		if (State.mExternalDeviceProvider && State.mExternalDeviceProvider != &Provider)
		{
			State.mError = "The specified external device provider is not registered.";
			return false;
		}
		State.mExternalDeviceProvider = nullptr;
		State.mError.clear();
		return true;
	}

	const IArdaExternalDeviceProvider* GetExternalDeviceProvider() noexcept
	{
		auto& State = BackendState();
		std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
		return State.mExternalDeviceProvider;
	}

	namespace
	{
		const char* UpdateResourceProvider(FArdaResourceProviderRegistry& Registry,
		    IArdaExternalResourceProvider& Provider,
		    bool bRegister)
		{
			const char* Name = Provider.GetName();
			if (!Name || !*Name)
			{
				return "External resource provider name must be non-empty.";
			}
			std::lock_guard<std::mutex> Lock(Registry.mMutex);
			for (auto It = Registry.mEntries.begin(); It != Registry.mEntries.end(); ++It)
			{
				if (It->mName != Name)
				{
					continue;
				}
				if (It->mProvider != &Provider)
				{
					return bRegister ? "An external resource provider with that name is already registered."
					                 : "The named external resource provider is a different object.";
				}
				if (!bRegister)
				{
					Registry.mEntries.erase(It);
				}
				return nullptr;
			}
			if (bRegister)
			{
				Registry.mEntries.push_back({Name, &Provider});
			}
			return nullptr;
		}

		bool UpdateExternalResourceProvider(IArdaExternalResourceProvider& Provider, bool bRegister)
		{
			auto& State = BackendState();
			const char* Error = UpdateResourceProvider(State.mResourceProviders, Provider, bRegister);
			// A host resolver may query the backend. Release its registry lock before taking the lifecycle lock.
			std::lock_guard<std::mutex> Lock(State.mLifecycleMutex);
			State.mError = Error ? Error : "";
			return Error == nullptr;
		}
	}

	bool RegisterExternalResourceProvider(IArdaExternalResourceProvider& Provider)
	{
		return UpdateExternalResourceProvider(Provider, true);
	}

	bool UnregisterExternalResourceProvider(IArdaExternalResourceProvider& Provider)
	{
		return UpdateExternalResourceProvider(Provider, false);
	}

	const IArdaExternalResourceProvider* GetExternalResourceProvider(const char* Name) noexcept
	{
		if (!Name || !Name[0])
		{
			return nullptr;
		}
		auto& Registry = GetResourceProviderRegistry();
		std::lock_guard<std::mutex> Lock(Registry.mMutex);
		for (const auto& Entry : Registry.mEntries)
		{
			if (Entry.mName == Name)
			{
				return Entry.mProvider;
			}
		}
		return nullptr;
	}

}
