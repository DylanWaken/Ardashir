#include "ArdaBackendCorePch.h"

#include "ArdaExternalInterop.h"

namespace arda::backend
{
    void SetBackendError(const char* Error);

    namespace
    {
        struct FArdaResourceProviderEntry
        {
            eastl::string mName;
            IArdaExternalResourceProvider* mProvider = nullptr;
        };

        struct FArdaResourceProviderRegistry
        {
            std::mutex mMutex;
            eastl::vector<FArdaResourceProviderEntry> mEntries;
        };

        FArdaResourceProviderRegistry& GetResourceProviderRegistry()
        {
            static FArdaResourceProviderRegistry Registry;
            return Registry;
        }

        template <typename Ref, typename Desc, typename Resolver, typename Importer>
        rhi::TArdaRHIResult<Ref> ImportExternalResource(
            const char* ProviderName,
            uint64_t Id,
            Resolver Resolve,
            Importer Import)
        {
            if (!ProviderName || !ProviderName[0])
            {
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidArgument,
                    "External resource provider name must be non-empty.") };
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
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidArgument,
                    "External resource provider is not registered.") };
            }
            if (!IsBackendInitialized())
            {
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidState,
                    "External resources require an initialized backend.") };
            }
            if (Provider->GetBackendType() != GetDeviceContext().mBackend)
            {
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::WrongDevice,
                    "External resource provider backend does not match the active device.") };
            }
            const char* RequiredBackendName = Provider->GetBackendName();
            if (RequiredBackendName && RequiredBackendName[0] &&
                GetDeviceContext().mBackendName != RequiredBackendName)
            {
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::WrongDevice,
                    "External resource provider module does not match the active device.") };
            }

            Desc Description;
            rhi::FArdaRHIStatus Status = (Provider->*Resolve)(Id, Description);
            if (!Status)
            {
                return { {}, eastl::move(Status) };
            }
            rhi::FArdaRHIDeviceRef Device = GetDevice();
            if (!Device)
            {
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidState,
                    "The active backend has no RHI device.") };
            }
            return (Device.Get()->*Import)(Description);
        }
    }

    bool RegisterExternalResourceProvider(IArdaExternalResourceProvider& Provider)
    {
        const char* Name = Provider.GetName();
        if (!Name || !Name[0])
        {
            SetBackendError("External resource provider name must be non-empty.");
            return false;
        }
        auto& Registry = GetResourceProviderRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        for (const auto& Entry : Registry.mEntries)
        {
            if (Entry.mName == Name)
            {
                if (Entry.mProvider == &Provider)
                {
                    SetBackendError("");
                    return true;
                }
                SetBackendError(
                    "An external resource provider with that name is already registered.");
                return false;
            }
        }
        Registry.mEntries.push_back({ Name, &Provider });
        SetBackendError("");
        return true;
    }

    bool UnregisterExternalResourceProvider(IArdaExternalResourceProvider& Provider)
    {
        const char* Name = Provider.GetName();
        if (!Name || !Name[0])
        {
            SetBackendError("External resource provider name must be non-empty.");
            return false;
        }
        auto& Registry = GetResourceProviderRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        for (auto It = Registry.mEntries.begin(); It != Registry.mEntries.end(); ++It)
        {
            if (It->mName != Name)
            {
                continue;
            }
            if (It->mProvider != &Provider)
            {
                SetBackendError(
                    "The named external resource provider is a different object.");
                return false;
            }
            Registry.mEntries.erase(It);
            SetBackendError("");
            return true;
        }
        SetBackendError("");
        return true;
    }

    const IArdaExternalResourceProvider* GetExternalResourceProvider(
        const char* Name) noexcept
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

    rhi::TArdaRHIResult<rhi::FArdaRHITextureRef> ImportExternalTexture(
        const char* ProviderName,
        uint64_t Id)
    {
        return ImportExternalResource<
            rhi::FArdaRHITextureRef,
            rhi::FArdaRHINativeTextureImportDesc>(
            ProviderName,
            Id,
            &IArdaExternalResourceProvider::ResolveNativeTexture,
            &rhi::IArdaRHIDevice::ImportNativeTexture);
    }

    rhi::TArdaRHIResult<rhi::FArdaRHIBufferRef> ImportExternalBuffer(
        const char* ProviderName,
        uint64_t Id)
    {
        return ImportExternalResource<
            rhi::FArdaRHIBufferRef,
            rhi::FArdaRHINativeBufferImportDesc>(
            ProviderName,
            Id,
            &IArdaExternalResourceProvider::ResolveNativeBuffer,
            &rhi::IArdaRHIDevice::ImportNativeBuffer);
    }
}
