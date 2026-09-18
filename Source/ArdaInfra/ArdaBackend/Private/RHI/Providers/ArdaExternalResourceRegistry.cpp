#include "RHI/Providers/ArdaExternalResourceProvider.h"
#include "RHI/Context/ArdaExternalResourceContext.h"

namespace arda
{
	void SetBackendError(const char* Error);

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
				SetBackendError("An external resource provider with that name is already registered.");
				return false;
			}
		}
		Registry.mEntries.push_back({Name, &Provider});
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
				SetBackendError("The named external resource provider is a different object.");
				return false;
			}
			Registry.mEntries.erase(It);
			SetBackendError("");
			return true;
		}
		SetBackendError("");
		return true;
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
