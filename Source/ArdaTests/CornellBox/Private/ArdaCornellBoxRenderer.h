#pragma once

#include "ArdaBackend.h"
#include "ArdaSwapChain.h"
#include "ArdaRenderGraph.h"
#include "PipelineStateCache/ArdaPipelineStateCache.h"

#include <EASTL/string.h>
#include <memory>

namespace arda::tests::cornell_box
{
    struct FArdaCornellBoxSettings
    {
        /** Independent path samples launched in parallel per pixel and dispatch. */
        uint32_t mSamplesPerDispatch = 8;
        uint32_t mMaxSamples = 1024;
        uint32_t mMaxBounces = 12;
        uint32_t mSeed = 1;
        float mExposure = 1.0f;
        bool mbCompactStaticBlas = true;
    };

    class FArdaCornellBoxRenderer final
    {
    public:
        bool Initialize(
            rhi::FArdaRHIDeviceRef Device,
            rhi::EArdaRHIFormat SwapChainFormat,
            const FArdaCornellBoxSettings& Settings);

        /** Updates the free-flight camera and resets progressive accumulation on motion. */
        void UpdateCamera(
            float Forward,
            float Right,
            float LookX,
            float LookY,
            float DeltaSeconds);

        /** Invalidates resolution-dependent progressive state after swap-chain resize. */
        void NotifyResize();

        bool RenderFrame(backend::IArdaSwapChain& SwapChain);

        [[nodiscard]] const eastl::string& GetError() const noexcept
        {
            return mError;
        }

        [[nodiscard]] uint32_t GetAccumulatedSamples() const noexcept
        {
            return mAccumulatedSamples;
        }

    private:
        bool CreateShadersAndPipelines(rhi::EArdaRHIFormat SwapChainFormat);
        bool GenerateSceneGeometry();
        bool BuildSceneAccelerationStructures();
        bool BuildUncompactedSceneAccelerationStructures();
        bool CompactBlasAndBuildTlas(uint64_t CompactedSize);
        bool ExecuteGraph(
            render_graph::FARDGBuilder& Graph,
            const char* Description);
        [[nodiscard]] render_graph::FARDGRenderGraphContext
        CreateGraphContext() const;
        void ResetAccumulation();

        rhi::FArdaRHIDeviceRef mDevice;
        backend::FArdaGlobalShaderMap mShaderMap;
        std::unique_ptr<backend::FArdaPipelineStateCache> mPipelineStateCache;

        const backend::FArdaGlobalShaderInstance* mGenerateGeometryShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mRayGenerationShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mMissShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mClosestHitShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mAccumulateShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mPresentVertexShader = nullptr;
        const backend::FArdaGlobalShaderInstance* mPresentPixelShader = nullptr;
        backend::FArdaComputePipelineStateInitializer
            mGenerateGeometryPipelineInitializer;
        backend::FArdaComputePipelineStateInitializer
            mAccumulatePipelineInitializer;
        backend::FArdaGraphicsPipelineStateInitializer
            mPresentPipelineInitializer;
        rhi::FArdaRHIRayTracingPipelineRef mRayTracingPipeline;
        rhi::FArdaRHIShaderTableRef mShaderTable;

        rhi::FArdaRHIBufferRef mVertexBuffer;
        rhi::FArdaRHIBufferRef mIndexBuffer;
        rhi::FArdaRHIBufferRef mMaterialBuffer;
        rhi::FArdaRHIAccelStructRef mBlas;
        rhi::FArdaRHIAccelStructRef mTlas;
        rhi::FArdaRHITextureRef mAccumulationTexture;

        FArdaCornellBoxSettings mSettings;
        float mCameraPosition[3] = {0.0f, -2.65f, 1.0f};
        float mCameraYaw = 1.57079632679f;
        float mCameraPitch = 0.0f;
        uint32_t mAccumulatedSamples = 0;
        uint32_t mFrameIndex = 0;
        bool mbSceneReady = false;
        eastl::string mError;
    };
}
