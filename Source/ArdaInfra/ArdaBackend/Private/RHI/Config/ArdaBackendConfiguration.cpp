#include <mutex>
#include "RHI/Config/ArdaBackendCorePch.h"
#include "RHI/Config/ArdaBackendConfigurationPrivate.h"
#include "RHI/Interop/ArdaExternalInterop.h"
#include "RHI/Providers/ArdaExternalDeviceProvider.h"
#include "RHI/Providers/ArdaLinkedBackends.h"
namespace arda
{
	bool ResolveConfiguration(FArdaBackendContext& State,
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

	bool ConfigureBackend(const FArdaBackendConfiguration& configuration)
	{
		auto& state = GetBackendContext();
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
		return GetBackendContext().mConfiguration;
	}

}
