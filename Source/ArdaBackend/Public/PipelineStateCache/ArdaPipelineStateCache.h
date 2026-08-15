#pragma once

#include "PipelineStateCache/ArdaPipelineStateInitializer.h"
#include "RHIWrappers/ArdaRHIDevice.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace arda::backend
{
    enum class EArdaPipelineStateKind : uint8_t
    {
        Compute,
        Graphics
    };

    struct FArdaPipelineStateCacheConfiguration
    {
        size_t mMaxComputeEntries = 128;
        size_t mMaxGraphicsEntries = 128;
        size_t mMaxDiagnostics = 64;
    };

    struct FArdaPipelineStateCacheStats
    {
        uint64_t mHits = 0;
        uint64_t mMisses = 0;
        uint64_t mWaits = 0;
        uint64_t mCreateFailures = 0;
        size_t mInFlight = 0;
        size_t mComputeEntries = 0;
        size_t mGraphicsEntries = 0;
    };

    struct FArdaPipelineStateDiagnostic
    {
        EArdaPipelineStateKind mKind = EArdaPipelineStateKind::Compute;
        rhi::EArdaRHIResult mCode = rhi::EArdaRHIResult::Success;
        size_t mDescriptorHash = 0;
        eastl::string mDebugName;
        eastl::string mMessage;
    };

    /**
     * Thread-safe renderer-facing PSO cache bound permanently to one RHI device.
     * The device may itself perform lower-level descriptor caching.
     */
    class FArdaPipelineStateCache final
    {
    public:
        explicit FArdaPipelineStateCache(
            rhi::FArdaRHIDeviceRef Device,
            FArdaPipelineStateCacheConfiguration Configuration = {});
        ~FArdaPipelineStateCache();

        FArdaPipelineStateCache(const FArdaPipelineStateCache&) = delete;
        FArdaPipelineStateCache& operator=(const FArdaPipelineStateCache&) = delete;
        FArdaPipelineStateCache(FArdaPipelineStateCache&&) = delete;
        FArdaPipelineStateCache& operator=(FArdaPipelineStateCache&&) = delete;

        [[nodiscard]] rhi::FArdaRHIStatus GetOrCreateCompute(
            const FArdaComputePipelineStateInitializer& Initializer,
            rhi::FArdaRHIComputePipelineRef& OutPipeline,
            const rhi::IArdaRHIDevice* RequestingDevice = nullptr);
        [[nodiscard]] rhi::FArdaRHIStatus GetOrCreateGraphics(
            const FArdaGraphicsPipelineStateInitializer& Initializer,
            const rhi::FArdaRHIFramebufferRef& Framebuffer,
            rhi::FArdaRHIGraphicsPipelineRef& OutPipeline,
            const rhi::IArdaRHIDevice* RequestingDevice = nullptr);

        [[nodiscard]] rhi::FArdaRHIStatus PrecacheCompute(
            const FArdaComputePipelineStateInitializer& Initializer,
            const rhi::IArdaRHIDevice* RequestingDevice = nullptr);
        [[nodiscard]] rhi::FArdaRHIStatus PrecacheGraphics(
            const FArdaGraphicsPipelineStateInitializer& Initializer,
            const rhi::FArdaRHIFramebufferRef& Framebuffer,
            const rhi::IArdaRHIDevice* RequestingDevice = nullptr);

        [[nodiscard]] rhi::FArdaRHIStatus SetComputePipelineState(
            rhi::IArdaRHICommandList& CommandList,
            const FArdaComputePipelineStateInitializer& Initializer,
            rhi::FArdaRHIComputeState State);
        [[nodiscard]] rhi::FArdaRHIStatus SetGraphicsPipelineState(
            rhi::IArdaRHICommandList& CommandList,
            const FArdaGraphicsPipelineStateInitializer& Initializer,
            rhi::FArdaRHIGraphicsState State);

        void Trim(size_t MaxComputeEntries, size_t MaxGraphicsEntries);
        void Clear();

        [[nodiscard]] FArdaPipelineStateCacheStats GetStats() const;
        [[nodiscard]] eastl::vector<FArdaPipelineStateDiagnostic> GetDiagnostics() const;
        [[nodiscard]] const rhi::IArdaRHIDevice* GetDevice() const noexcept;

    private:
        struct FImpl;
        std::unique_ptr<FImpl> mImpl;
    };
}
