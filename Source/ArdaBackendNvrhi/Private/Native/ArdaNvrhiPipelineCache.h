/** @file ArdaNvrhiPipelineCache.h
 *  Declares Ardashir-owned native pipeline-cache integration for stock NVRHI.
 */
#pragma once

#include "ArdaDevice.h"

#include <EASTL/shared_ptr.h>
#include <filesystem>

#if defined(_WIN32) && defined(ARDA_NVRHI_WITH_D3D12)
struct ID3D12Device;
#endif

#if defined(ARDA_NVRHI_WITH_VULKAN)
#include <vulkan/vulkan.h>
#endif

namespace arda::backend
{
    class IArdaDiagnosticCallback;
}

namespace arda::rhi::private_impl
{
    /** Backend-local bridge that adds native caching without changing NVRHI. */
    class IArdaNvrhiPipelineCache
    {
    public:
        virtual ~IArdaNvrhiPipelineCache() = default;

        [[nodiscard]] virtual bool IsSupported() const noexcept = 0;
        virtual void SetPipelineKey(uint64_t Key) noexcept = 0;
        virtual void ClearPipelineKey() noexcept = 0;
        virtual void MarkDirty() noexcept = 0;
        virtual void FlushAndDisable() noexcept = 0;

#if defined(_WIN32) && defined(ARDA_NVRHI_WITH_D3D12)
        [[nodiscard]] virtual ID3D12Device* GetD3D12DeviceForNvrhi() noexcept
        {
            return nullptr;
        }
#endif
    };

    class FArdaNvrhiPipelineKeyScope
    {
    public:
        FArdaNvrhiPipelineKeyScope(
            IArdaNvrhiPipelineCache* Cache,
            uint64_t Key) noexcept
            : mCache(Cache)
        {
            if (mCache)
                mCache->SetPipelineKey(Key);
        }

        ~FArdaNvrhiPipelineKeyScope()
        {
            if (mCache)
                mCache->ClearPipelineKey();
        }

        FArdaNvrhiPipelineKeyScope(const FArdaNvrhiPipelineKeyScope&) = delete;
        FArdaNvrhiPipelineKeyScope& operator=(
            const FArdaNvrhiPipelineKeyScope&) = delete;

    private:
        IArdaNvrhiPipelineCache* mCache = nullptr;
    };

#if defined(_WIN32) && defined(ARDA_NVRHI_WITH_D3D12)
    [[nodiscard]] eastl::shared_ptr<IArdaNvrhiPipelineCache>
    CreateArdaNvrhiD3D12PipelineCache(
        ID3D12Device* Device,
        const eastl::string& BackendName,
        const std::filesystem::path& Directory,
        backend::IArdaDiagnosticCallback* DiagnosticCallback);
#endif

#if defined(ARDA_NVRHI_WITH_VULKAN)
    [[nodiscard]] eastl::shared_ptr<IArdaNvrhiPipelineCache>
    CreateArdaNvrhiVulkanPipelineCache(
        VkDevice Device,
        const VkAllocationCallbacks* AllocationCallbacks,
        const eastl::string& BackendName,
        const std::filesystem::path& Directory,
        backend::IArdaDiagnosticCallback* DiagnosticCallback);
#endif
}
