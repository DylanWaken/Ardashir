#include "ArdaBackendCorePch.h"

#include "ArdaBackendRegistry.h"
#include "ArdaLinkedBackends.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <atomic>
#include <mutex>
#include <limits>

namespace arda::backend
{
    namespace
    {
        struct FArdaBackendModuleEntry
        {
            IArdaBackendModule* mModule = nullptr;
        };

        struct FArdaBackendModuleRegistry
        {
            std::mutex mMutex;
            eastl::vector<FArdaBackendModuleEntry> mEntries;
            std::atomic<const IArdaBackendModule*> mActiveModule{ nullptr };
        };

        FArdaBackendModuleRegistry& GetBackendModuleRegistry()
        {
            static FArdaBackendModuleRegistry Registry;
            return Registry;
        }

        bool IsDescriptorValid(const FArdaBackendModuleDescriptor& Descriptor)
        {
            return Descriptor.mInterfaceVersion ==
                    ArdaBackendProviderInterfaceVersion &&
                !Descriptor.mName.empty() &&
                !Descriptor.mShaderArtifactExtension.empty() &&
                Descriptor.mShaderArtifactExtension.front() == '.';
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
        for (const FArdaBackendModuleEntry& Entry : Registry.mEntries)
        {
            if (Entry.mModule->GetDescriptor().mName != Descriptor.mName)
            {
                continue;
            }
            if (Entry.mModule == &Module)
            {
                return true;
            }
            return false;
        }

        Registry.mEntries.push_back({ &Module });
        eastl::sort(
            Registry.mEntries.begin(),
            Registry.mEntries.end(),
            [](const FArdaBackendModuleEntry& Left, const FArdaBackendModuleEntry& Right)
            {
                return Left.mModule->GetDescriptor().mName <
                    Right.mModule->GetDescriptor().mName;
            });
        return true;
    }

    bool UnregisterBackendModule(IArdaBackendModule& Module)
    {
        auto& Registry = GetBackendModuleRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        if (Registry.mActiveModule.load(std::memory_order_acquire) == &Module)
        {
            return false;
        }
        for (auto It = Registry.mEntries.begin(); It != Registry.mEntries.end(); ++It)
        {
            if (It->mModule == &Module)
            {
                Registry.mEntries.erase(It);
                return true;
            }
        }
        return true;
    }

    IArdaBackendModule* FindBackendModule(const char* Name) noexcept
    {
        private_api::RegisterLinkedBackendModules();
        if (!Name || !Name[0])
        {
            return nullptr;
        }
        auto& Registry = GetBackendModuleRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        for (const FArdaBackendModuleEntry& Entry : Registry.mEntries)
        {
            if (Entry.mModule->GetDescriptor().mName == Name)
            {
                return Entry.mModule;
            }
        }
        return nullptr;
    }

    IArdaBackendModule* FindDefaultBackendModule() noexcept
    {
        private_api::RegisterLinkedBackendModules();
        auto& Registry = GetBackendModuleRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        IArdaBackendModule* BestModule = nullptr;
        int32_t BestPriority = std::numeric_limits<int32_t>::min();
        for (const FArdaBackendModuleEntry& Entry : Registry.mEntries)
        {
            const FArdaBackendModuleDescriptor& Descriptor =
                Entry.mModule->GetDescriptor();
            if (Descriptor.mPriority > BestPriority)
            {
                BestModule = Entry.mModule;
                BestPriority = Descriptor.mPriority;
            }
        }
        return BestModule;
    }

    namespace
    {
        bool CopyShaderTarget(
            const IArdaBackendModule* Module,
            FArdaShaderTarget& OutTarget) noexcept
        {
            OutTarget = {};
            if (!Module)
                return false;
            const FArdaBackendModuleDescriptor& Descriptor = Module->GetDescriptor();
            OutTarget.mBackendName = Descriptor.mName;
            OutTarget.mBinaryFormat = Descriptor.mShaderBinaryFormat;
            OutTarget.mArtifactExtension = Descriptor.mShaderArtifactExtension;
            OutTarget.mCompilerIdentity = Descriptor.mShaderCompilerIdentity;
            return static_cast<bool>(OutTarget);
        }
    }

    bool ResolveShaderTarget(
        const char* BackendName,
        FArdaShaderTarget& OutTarget) noexcept
    {
        return CopyShaderTarget(FindBackendModule(BackendName), OutTarget);
    }

    bool ResolveDefaultShaderTarget(FArdaShaderTarget& OutTarget) noexcept
    {
        return CopyShaderTarget(FindDefaultBackendModule(), OutTarget);
    }

    eastl::vector<FArdaBackendModuleDescriptor> EnumerateBackendModules()
    {
        private_api::RegisterLinkedBackendModules();
        auto& Registry = GetBackendModuleRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        eastl::vector<FArdaBackendModuleDescriptor> Result;
        Result.reserve(Registry.mEntries.size());
        for (const FArdaBackendModuleEntry& Entry : Registry.mEntries)
        {
            Result.push_back(Entry.mModule->GetDescriptor());
        }
        return Result;
    }

    const IArdaBackendModule* GetActiveBackendModule() noexcept
    {
        return GetBackendModuleRegistry().mActiveModule.load(std::memory_order_acquire);
    }

    namespace private_api
    {
        void SetActiveBackendModule(const IArdaBackendModule* Module) noexcept
        {
            GetBackendModuleRegistry().mActiveModule.store(
                Module, std::memory_order_release);
        }
    }
}
