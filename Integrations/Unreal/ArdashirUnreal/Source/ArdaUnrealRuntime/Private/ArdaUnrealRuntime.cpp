#include "ArdaUnrealRuntime.h"

#include "BackendImpl/ArdaUnrealBackend.h"

#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"

#include "RHI/Device/ArdaBackendDevice.h"

DEFINE_LOG_CATEGORY_STATIC(LogArdaUnreal, Log, All);

namespace arda
{
	namespace
	{
		FString GArdaUnrealInitializationError;

		bool IsUnrealBackendActive()
		{
			return IsBackendInitialized() && GetBackendConfiguration().mBackendName == "unreal-rhi";
		}
	}

	bool InitializeUnrealBackend()
	{
		check(IsInRenderingThread());
		GArdaUnrealInitializationError.Reset();
		if (IsBackendInitialized())
		{
			if (IsUnrealBackendActive())
			{
				return true;
			}
			GArdaUnrealInitializationError =
			    TEXT("Another Arda backend is already initialized; shut it down before selecting Unreal RHI.");
			return false;
		}

		FArdaBackendConfiguration Configuration;
		Configuration.mBackendName = "unreal-rhi";
		Configuration.mDeviceSource = EArdaDeviceSource::ExternalProvider;
		Configuration.mbEnableValidation = false;
		Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
		Configuration.mPipelineCacheDirectory.clear();
		const FString CacheDirectory =
		    FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Ardashir/ShaderCache"));
		Configuration.mShaderCacheDirectory = std::filesystem::path(*CacheDirectory);
		return ConfigureBackend(Configuration) && InitializeBackend();
	}

	void ShutdownUnrealBackend() noexcept
	{
		if (IsUnrealBackendActive())
		{
			ShutdownBackend();
		}
	}

	FArdaRHIDeviceRef GetUnrealBackendDevice() noexcept
	{
		return IsUnrealBackendActive() ? GetDevice() : FArdaRHIDeviceRef{};
	}

	FString GetUnrealBackendError()
	{
		if (!GArdaUnrealInitializationError.IsEmpty())
		{
			return GArdaUnrealInitializationError;
		}
		const eastl::string Error = GetBackendError();
		return FString(UTF8_TO_TCHAR(Error.c_str()));
	}

	class FArdaUnrealRuntimeModule final : public IModuleInterface
	{
	public:
		void StartupModule() override
		{
			if (!RegisterUnrealRHIBackend())
			{
				UE_LOG(LogArdaUnreal, Error, TEXT("Cannot register Unreal RHI backend: %s"), *GetUnrealBackendError());
			}
		}

		void ShutdownModule() override
		{
			ShutdownUnrealBackend();
			if (!UnregisterUnrealRHIBackend())
			{
				UE_LOG(LogArdaUnreal,
				    Warning,
				    TEXT("Cannot unregister Unreal RHI backend: %s"),
				    *GetUnrealBackendError());
			}
		}
	};
}

IMPLEMENT_MODULE(arda::FArdaUnrealRuntimeModule, ArdaUnrealRuntime)
