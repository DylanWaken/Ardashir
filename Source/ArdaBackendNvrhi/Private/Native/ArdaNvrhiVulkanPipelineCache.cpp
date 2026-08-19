#include "ArdaNvrhiPch.h"

#include "Native/ArdaNvrhiPipelineCache.h"
#include "Native/ArdaNvrhiPipelineCacheCommon.h"

#if defined(ARDA_NVRHI_WITH_VULKAN)

#include <unordered_map>
#include <vector>

namespace arda::rhi::private_impl
{
    namespace
    {
        struct FVulkanCacheEntry
        {
            VkPipelineCache mCache = VK_NULL_HANDLE;
        };

        std::mutex GHookMutex;
        std::unordered_map<VkDevice, FVulkanCacheEntry> GCacheByDevice;
        PFN_vkCreateGraphicsPipelines GCreateGraphicsPipelines = nullptr;
        PFN_vkCreateComputePipelines GCreateComputePipelines = nullptr;
        PFN_vkCreateRayTracingPipelinesKHR GCreateRayTracingPipelines = nullptr;

        VkPipelineCache FindCache(
            VkDevice Device,
            VkPipelineCache Fallback) noexcept
        {
            std::lock_guard<std::mutex> Lock(GHookMutex);
            const auto It = GCacheByDevice.find(Device);
            return It == GCacheByDevice.end()
                ? Fallback
                : It->second.mCache;
        }

        VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelinesHook(
            VkDevice Device,
            VkPipelineCache PipelineCache,
            uint32_t Count,
            const VkGraphicsPipelineCreateInfo* CreateInfos,
            const VkAllocationCallbacks* Allocator,
            VkPipeline* Pipelines)
        {
            return GCreateGraphicsPipelines(
                Device, FindCache(Device, PipelineCache), Count,
                CreateInfos, Allocator, Pipelines);
        }

        VKAPI_ATTR VkResult VKAPI_CALL CreateComputePipelinesHook(
            VkDevice Device,
            VkPipelineCache PipelineCache,
            uint32_t Count,
            const VkComputePipelineCreateInfo* CreateInfos,
            const VkAllocationCallbacks* Allocator,
            VkPipeline* Pipelines)
        {
            return GCreateComputePipelines(
                Device, FindCache(Device, PipelineCache), Count,
                CreateInfos, Allocator, Pipelines);
        }

        VKAPI_ATTR VkResult VKAPI_CALL CreateRayTracingPipelinesHook(
            VkDevice Device,
            VkDeferredOperationKHR DeferredOperation,
            VkPipelineCache PipelineCache,
            uint32_t Count,
            const VkRayTracingPipelineCreateInfoKHR* CreateInfos,
            const VkAllocationCallbacks* Allocator,
            VkPipeline* Pipelines)
        {
            return GCreateRayTracingPipelines(
                Device, DeferredOperation,
                FindCache(Device, PipelineCache), Count,
                CreateInfos, Allocator, Pipelines);
        }

        void RegisterPipelineHooks(VkDevice Device, VkPipelineCache Cache)
        {
            std::lock_guard<std::mutex> Lock(GHookMutex);
            if (GCacheByDevice.empty())
            {
                GCreateGraphicsPipelines =
                    VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateGraphicsPipelines;
                GCreateComputePipelines =
                    VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateComputePipelines;
                GCreateRayTracingPipelines =
                    VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateRayTracingPipelinesKHR;
                VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateGraphicsPipelines =
                    CreateGraphicsPipelinesHook;
                VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateComputePipelines =
                    CreateComputePipelinesHook;
                if (GCreateRayTracingPipelines)
                {
                    VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateRayTracingPipelinesKHR =
                        CreateRayTracingPipelinesHook;
                }
            }
            GCacheByDevice[Device] = { Cache };
        }

        void UnregisterPipelineHooks(VkDevice Device)
        {
            std::lock_guard<std::mutex> Lock(GHookMutex);
            GCacheByDevice.erase(Device);
            if (GCacheByDevice.empty())
            {
                VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateGraphicsPipelines =
                    GCreateGraphicsPipelines;
                VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateComputePipelines =
                    GCreateComputePipelines;
                VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateRayTracingPipelinesKHR =
                    GCreateRayTracingPipelines;
                GCreateGraphicsPipelines = nullptr;
                GCreateComputePipelines = nullptr;
                GCreateRayTracingPipelines = nullptr;
            }
        }

        class FVulkanPipelineCache final : public IArdaNvrhiPipelineCache
        {
        public:
            FVulkanPipelineCache(
                VkDevice Device,
                const VkAllocationCallbacks* AllocationCallbacks,
                eastl::string BackendName,
                std::filesystem::path Directory,
                backend::IArdaDiagnosticCallback* DiagnosticCallback)
                : mDevice(Device)
                , mAllocationCallbacks(AllocationCallbacks)
                , mBackendName(eastl::move(BackendName))
                , mDirectory(std::move(Directory))
                , mDiagnosticCallback(DiagnosticCallback)
                , mCreatePipelineCache(
                    VULKAN_HPP_DEFAULT_DISPATCHER.vkCreatePipelineCache)
                , mDestroyPipelineCache(
                    VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyPipelineCache)
                , mGetPipelineCacheData(
                    VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPipelineCacheData)
            {
                mbSupported = mDevice && mCreatePipelineCache &&
                    mDestroyPipelineCache && mGetPipelineCacheData &&
                    VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateGraphicsPipelines &&
                    VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateComputePipelines;
                if (!mbSupported || mDirectory.empty())
                    return;

                std::vector<uint8_t> Payload;
                const auto Path = pipeline_cache::MakePath(
                    mDirectory, mBackendName);
                std::error_code Error;
                const bool Exists = std::filesystem::exists(Path, Error);
                const bool Valid = Exists &&
                    pipeline_cache::ReadBlob(Path, mBackendName, Payload);

                VkPipelineCacheCreateInfo Description{
                    VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
                if (Valid)
                {
                    Description.initialDataSize = Payload.size();
                    Description.pInitialData = Payload.data();
                }
                VkResult Result = mCreatePipelineCache(
                    mDevice, &Description, mAllocationCallbacks, &mCache);
                if (Result != VK_SUCCESS && Valid)
                {
                    pipeline_cache::Warn(
                        mDiagnosticCallback,
                        "Vulkan rejected persistent pipeline cache data; using an empty cache.");
                    Description.initialDataSize = 0;
                    Description.pInitialData = nullptr;
                    Result = mCreatePipelineCache(
                        mDevice, &Description, mAllocationCallbacks, &mCache);
                }
                else if (Valid)
                {
                    pipeline_cache::Warn(
                        mDiagnosticCallback,
                        "Vulkan persistent pipeline cache data was accepted.");
                }
                else if (Exists)
                {
                    pipeline_cache::Warn(
                        mDiagnosticCallback,
                        "Ignoring a corrupt, truncated, or wrong-backend pipeline cache blob.");
                }

                if (Result == VK_SUCCESS && mCache)
                {
                    RegisterPipelineHooks(mDevice, mCache);
                    mbEnabled = true;
                }
                else
                {
                    pipeline_cache::Warn(
                        mDiagnosticCallback,
                        "Failed to create the Arda-owned Vulkan pipeline cache.");
                }
            }

            ~FVulkanPipelineCache() override
            {
                FlushAndDisable();
            }

            bool IsSupported() const noexcept override
            {
                return mbSupported;
            }

            void SetPipelineKey(uint64_t) noexcept override {}
            void ClearPipelineKey() noexcept override {}

            void MarkDirty() noexcept override
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                if (mbEnabled)
                    mbDirty = true;
            }

            void FlushAndDisable() noexcept override
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                if (!mbEnabled)
                    return;

                if (mbDirty)
                {
                    size_t Size = 0;
                    VkResult Result = mGetPipelineCacheData(
                        mDevice, mCache, &Size, nullptr);
                    if (Result == VK_SUCCESS &&
                        Size <= pipeline_cache::MaxPayloadSize)
                    {
                        std::vector<uint8_t> Payload(Size);
                        Result = mGetPipelineCacheData(
                            mDevice, mCache, &Size, Payload.data());
                        if (Result == VK_SUCCESS)
                        {
                            Payload.resize(Size);
                            if (!pipeline_cache::WriteBlob(
                                    pipeline_cache::MakePath(
                                        mDirectory, mBackendName),
                                    mBackendName,
                                    Payload))
                            {
                                pipeline_cache::Warn(
                                    mDiagnosticCallback,
                                    "Failed to atomically save the Vulkan pipeline cache.");
                            }
                        }
                    }
                    else
                    {
                        pipeline_cache::Warn(
                            mDiagnosticCallback,
                            "Failed to query Vulkan pipeline cache data.");
                    }
                }

                UnregisterPipelineHooks(mDevice);
                mDestroyPipelineCache(
                    mDevice, mCache, mAllocationCallbacks);
                mCache = VK_NULL_HANDLE;
                mbEnabled = false;
                mbDirty = false;
                mDirectory.clear();
                mDiagnosticCallback = nullptr;
            }

        private:
            VkDevice mDevice = VK_NULL_HANDLE;
            const VkAllocationCallbacks* mAllocationCallbacks = nullptr;
            eastl::string mBackendName;
            std::filesystem::path mDirectory;
            backend::IArdaDiagnosticCallback* mDiagnosticCallback = nullptr;
            PFN_vkCreatePipelineCache mCreatePipelineCache = nullptr;
            PFN_vkDestroyPipelineCache mDestroyPipelineCache = nullptr;
            PFN_vkGetPipelineCacheData mGetPipelineCacheData = nullptr;
            VkPipelineCache mCache = VK_NULL_HANDLE;
            std::mutex mMutex;
            bool mbSupported = false;
            bool mbEnabled = false;
            bool mbDirty = false;
        };
    }

    eastl::shared_ptr<IArdaNvrhiPipelineCache>
    CreateArdaNvrhiVulkanPipelineCache(
        VkDevice Device,
        const VkAllocationCallbacks* AllocationCallbacks,
        const eastl::string& BackendName,
        const std::filesystem::path& Directory,
        backend::IArdaDiagnosticCallback* DiagnosticCallback)
    {
        return eastl::make_shared<FVulkanPipelineCache>(
            Device, AllocationCallbacks, BackendName, Directory,
            DiagnosticCallback);
    }
}

#endif
