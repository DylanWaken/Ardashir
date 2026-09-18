#include "RHI/Device/ArdaBackendDevice.h"
#include "RHI/Context/ArdaExternalResourceContext.h"

#include "RHI/Interop/ArdaExternalInterop.h"
#include "RHI/Providers/ArdaExternalResourceProvider.h"

namespace arda
{
	namespace
	{
		template <typename Ref, typename Desc, typename Resolver, typename Importer>
		arda::TArdaRHIResult<Ref> ImportExternalResource(const char* ProviderName,
		    uint64_t Id,
		    Resolver Resolve,
		    Importer Import)
		{
			if (!ProviderName || !ProviderName[0])
			{
				return {{},
				    arda::FArdaRHIStatus::Error(arda::EArdaRHIResult::InvalidArgument,
				        "External resource provider name must be non-empty.")};
			}

			auto& Registry = GetResourceProviderRegistry();
			std::lock_guard<std::mutex> Lock(Registry.mMutex);
			IArdaExternalResourceProvider* Provider = nullptr;
			for (const auto& Entry : Registry.mEntries)
			{
				if (Entry.mName == ProviderName)
				{
					Provider = Entry.mProvider;
					break;
				}
			}
			if (!Provider)
			{
				return {{},
				    arda::FArdaRHIStatus::Error(arda::EArdaRHIResult::InvalidArgument,
				        "External resource provider is not registered.")};
			}
			if (!IsBackendInitialized())
			{
				return {{},
				    arda::FArdaRHIStatus::Error(arda::EArdaRHIResult::InvalidState,
				        "External resources require an initialized backend.")};
			}
			const FArdaBackendConfiguration& Configuration = GetBackendConfiguration();
			const char* RequiredBackendName = Provider->GetBackendName();
			if (!RequiredBackendName || !RequiredBackendName[0] || Configuration.mBackendName != RequiredBackendName)
			{
				return {{},
				    arda::FArdaRHIStatus::Error(arda::EArdaRHIResult::WrongDevice,
				        "External resource provider module does not match the active device.")};
			}

			Desc Description;
			arda::FArdaRHIStatus Status = (Provider->*Resolve)(Id, Description);
			if (!Status)
			{
				return {{}, eastl::move(Status)};
			}
			arda::FArdaRHIDeviceRef Device = GetDevice();
			if (!Device)
			{
				return {{},
				    arda::FArdaRHIStatus::Error(arda::EArdaRHIResult::InvalidState,
				        "The active backend has no RHI device.")};
			}
			return (Device.Get()->*Import)(Description);
		}
	}

	arda::TArdaRHIResult<arda::FArdaRHITextureRef> ImportExternalTexture(const char* ProviderName, uint64_t Id)
	{
		return ImportExternalResource<arda::FArdaRHITextureRef, arda::FArdaRHINativeTextureImportDesc>(ProviderName,
		    Id,
		    &IArdaExternalResourceProvider::ResolveNativeTexture,
		    &arda::IArdaRHIDevice::ImportNativeTexture);
	}

	arda::TArdaRHIResult<arda::FArdaRHIBufferRef> ImportExternalBuffer(const char* ProviderName, uint64_t Id)
	{
		return ImportExternalResource<arda::FArdaRHIBufferRef, arda::FArdaRHINativeBufferImportDesc>(ProviderName,
		    Id,
		    &IArdaExternalResourceProvider::ResolveNativeBuffer,
		    &arda::IArdaRHIDevice::ImportNativeBuffer);
	}
}
