#include "RHI/Providers/ArdaBackendRegistry.h"
#include "RHI/Providers/ArdaLinkedBackends.h"

#include <EASTL/atomic.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/sort.h>
#include <EASTL/vector.h>

#include <mutex>

namespace arda
{
	namespace
	{
		struct FArdaBackendModuleRegistry
		{
			std::mutex mMutex;
			eastl::vector<IArdaBackendModule*> mModules;
			eastl::atomic<const IArdaBackendModule*> mActiveModule{nullptr};
		};

		FArdaBackendModuleRegistry& GetBackendModuleRegistry()
		{
			static FArdaBackendModuleRegistry Registry;
			return Registry;
		}

		bool IsDescriptorValid(const FArdaBackendModuleDescriptor& Descriptor)
		{
			return Descriptor.mInterfaceVersion == ArdaBackendProviderInterfaceVersion && !Descriptor.mName.empty() &&
			    !Descriptor.mShaderArtifactExtension.empty() && Descriptor.mShaderArtifactExtension.front() == '.';
		}
	}

	bool RegisterBackendModule(IArdaBackendModule& Module)
	{
		const FArdaBackendModuleDescriptor& Descriptor = Module.GetDescriptor();
		if (!IsDescriptorValid(Descriptor))
		{
			return false;
		}

		auto& Registry = GetBackendModuleRegistry();
		std::lock_guard<std::mutex> Lock(Registry.mMutex);
		for (const IArdaBackendModule* Existing : Registry.mModules)
		{
			if (Existing->GetDescriptor().mName != Descriptor.mName)
			{
				continue;
			}
			return Existing == &Module;
		}

		Registry.mModules.push_back(&Module);
		eastl::sort(Registry.mModules.begin(),
		    Registry.mModules.end(),
		    [](const IArdaBackendModule* Left, const IArdaBackendModule* Right)
		    {
			    return Left->GetDescriptor().mName < Right->GetDescriptor().mName;
		    });
		return true;
	}

	bool UnregisterBackendModule(IArdaBackendModule& Module)
	{
		auto& Registry = GetBackendModuleRegistry();
		std::lock_guard<std::mutex> Lock(Registry.mMutex);
		if (Registry.mActiveModule.load(eastl::memory_order_acquire) == &Module)
		{
			return false;
		}
		for (auto It = Registry.mModules.begin(); It != Registry.mModules.end(); ++It)
		{
			if (*It == &Module)
			{
				Registry.mModules.erase(It);
				return true;
			}
		}
		return true;
	}

	IArdaBackendModule* FindBackendModule(const char* Name) noexcept
	{
		arda::RegisterLinkedBackendModules();
		if (!Name || !Name[0])
		{
			return nullptr;
		}
		auto& Registry = GetBackendModuleRegistry();
		std::lock_guard<std::mutex> Lock(Registry.mMutex);
		for (IArdaBackendModule* Module : Registry.mModules)
		{
			if (Module->GetDescriptor().mName == Name)
			{
				return Module;
			}
		}
		return nullptr;
	}

	IArdaBackendModule* FindDefaultBackendModule() noexcept
	{
		arda::RegisterLinkedBackendModules();
		auto& Registry = GetBackendModuleRegistry();
		std::lock_guard<std::mutex> Lock(Registry.mMutex);
		IArdaBackendModule* BestModule = nullptr;
		int32_t BestPriority = eastl::numeric_limits<int32_t>::min();
		for (IArdaBackendModule* Module : Registry.mModules)
		{
			const FArdaBackendModuleDescriptor& Descriptor = Module->GetDescriptor();
			if (Descriptor.mPriority > BestPriority)
			{
				BestModule = Module;
				BestPriority = Descriptor.mPriority;
			}
		}
		return BestModule;
	}

	namespace
	{
		bool CopyShaderTarget(const IArdaBackendModule* Module, FArdaShaderTarget& OutTarget) noexcept
		{
			OutTarget = {};
			if (!Module)
			{
				return false;
			}
			const FArdaBackendModuleDescriptor& Descriptor = Module->GetDescriptor();
			OutTarget.mBackendName = Descriptor.mName;
			OutTarget.mBinaryFormat = Descriptor.mShaderBinaryFormat;
			OutTarget.mArtifactExtension = Descriptor.mShaderArtifactExtension;
			OutTarget.mCompilerIdentity = Descriptor.mShaderCompilerIdentity;
			return static_cast<bool>(OutTarget);
		}
	}

	bool ResolveShaderTarget(const char* BackendName, FArdaShaderTarget& OutTarget) noexcept
	{
		return CopyShaderTarget(FindBackendModule(BackendName), OutTarget);
	}

	bool ResolveDefaultShaderTarget(FArdaShaderTarget& OutTarget) noexcept
	{
		return CopyShaderTarget(FindDefaultBackendModule(), OutTarget);
	}

	eastl::vector<FArdaBackendModuleDescriptor> EnumerateBackendModules()
	{
		arda::RegisterLinkedBackendModules();
		auto& Registry = GetBackendModuleRegistry();
		std::lock_guard<std::mutex> Lock(Registry.mMutex);
		eastl::vector<FArdaBackendModuleDescriptor> Result;
		Result.reserve(Registry.mModules.size());
		for (const IArdaBackendModule* Module : Registry.mModules)
		{
			Result.push_back(Module->GetDescriptor());
		}
		return Result;
	}

	const IArdaBackendModule* GetActiveBackendModule() noexcept
	{
		return GetBackendModuleRegistry().mActiveModule.load(eastl::memory_order_acquire);
	}

	void SetActiveBackendModule(const IArdaBackendModule* Module) noexcept
	{
		GetBackendModuleRegistry().mActiveModule.store(Module, eastl::memory_order_release);
	}

}
