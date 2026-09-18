#include "ArdaBackendCorePch.h"

#include "RHI/Device/ArdaBackendDevice.h"
#include "RHI/Context/ArdaBackendContext.h"
#include "RHI/Config/ArdaBackendConfigurationPrivate.h"
#include "RHI/Providers/ArdaBackendRegistry.h"
#include "RHI/Interop/ArdaExternalInterop.h"
#include "RHI/Providers/ArdaLinkedBackends.h"
#include "RHI/Scheduling/ArdaSwapChain.h"
#include "RHI/Device/ArdaRHIDevicePrivate.h"
#include "RHI/Shaders/ArdaShaderCompiler.h"
#include "RHI/Shaders/ArdaShaderDirectoriesPrivate.h"

namespace arda
{
	void SetBackendError(const char* Error);


	namespace
	{
		EArdaInitializeResult CreateConfiguredDevices(FArdaBackendContext& State,
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

		void PublishInitializedDevice(FArdaBackendContext& State, IArdaBackendModule& Module)
		{
			arda::SetActiveBackendModule(&Module);
			State.mError.clear();
		}

		bool FreezeAndValidateShaderSources(FArdaBackendContext& State)
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

		bool BeginShaderDirectoryUse(FArdaBackendContext& State)
		{
			const FArdaShaderDirectoryStatus Status = arda::BeginShaderDirectoryRegistryUse();
			if (Status)
			{
				return true;
			}
			State.mError = eastl::string("Shader source directory registry is unavailable: ") + Status.mMessage;
			return false;
		}

		bool EnsureStartupShaders(FArdaBackendContext& State, const FArdaBackendConfiguration& Configuration)
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

		bool PrepareInitialization(FArdaBackendContext& State,
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

	bool InitializeBackend()
	{
		auto& state = GetBackendContext();
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
		auto& State = GetBackendContext();
		std::lock_guard<std::mutex> Lock(State.mMutex);
		return State.mInitializeResult;
	}

	EArdaInitializeResult InitializeBackendForPresentation(IArdaWindowSurface& WindowSurface,
	    uint32_t Width,
	    uint32_t Height,
	    eastl::unique_ptr<IArdaSwapChain>& OutSwapChain)
	{
		OutSwapChain.reset();

		auto& state = GetBackendContext();
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
		auto& state = GetBackendContext();
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
		return !GetBackendContext().mDevices.empty();
	}

	arda::FArdaRHIDeviceRef GetDevice() noexcept
	{
		return GetDevice(GetBackendContext().mConfiguration.mDefaultDeviceIndex);
	}

	FArdaRHIDeviceRef GetDevice(uint32_t DeviceIndex) noexcept
	{
		const auto& Devices = GetBackendContext().mDevices;
		return DeviceIndex < Devices.size() ? Devices[DeviceIndex].mPublic.mDevice : FArdaRHIDeviceRef{};
	}

	eastl::vector<FArdaBackendDevice> GetDevices()
	{
		auto& State = GetBackendContext();
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
		auto& State = GetBackendContext();
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
		auto& State = GetBackendContext();
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
		auto& state = GetBackendContext();
		std::lock_guard<std::mutex> lock(state.mMutex);
		return state.mError;
	}

	void SetBackendError(const char* Error)
	{
		auto& state = GetBackendContext();
		std::lock_guard<std::mutex> lock(state.mMutex);
		state.mError = Error ? Error : "";
	}

	const char* GetBackendModuleName() noexcept
	{
		return "ArdaBackend";
	}
}
