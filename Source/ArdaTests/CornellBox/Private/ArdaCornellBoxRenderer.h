#pragma once

#include "ArdaBackend.h"
#include "ArdaSwapChain.h"
#include "ArdaRenderGraph.h"
#include "PipelineStateCache/ArdaPipelineStateCache.h"

#include <EASTL/string.h>
#include <memory>

namespace arda
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
            arda::FArdaRHIDeviceRef Device,
            arda::EArdaRHIFormat SwapChainFormat,
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

        bool RenderFrame(arda::IArdaSwapChain& SwapChain);

        [[nodiscard]] const eastl::string& GetError() const noexcept
        {
            return mError;
        }

        [[nodiscard]] uint32_t GetAccumulatedSamples() const noexcept
        {
            return mAccumulatedSamples;
        }

    private:
        bool CreateShadersAndPipelines(arda::EArdaRHIFormat SwapChainFormat);
        bool GenerateSceneGeometry();
        bool BuildSceneAccelerationStructures();
        bool BuildUncompactedSceneAccelerationStructures();
        bool CompactBlasAndBuildTlas(uint64_t CompactedSize);
        bool ExecuteGraph(
            arda::FARDGBuilder& Graph,
            const char* Description);
        [[nodiscard]] arda::FARDGRenderGraphContext
        CreateGraphContext() const;
        void ResetAccumulation();

        arda::FArdaRHIDeviceRef mDevice;
        arda::FArdaGlobalShaderMap mShaderMap;
        std::unique_ptr<arda::FArdaPipelineStateCache> mPipelineStateCache;

        const arda::FArdaGlobalShaderInstance* mGenerateGeometryShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mRayGenerationShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mMissShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mClosestHitShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mAccumulateShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mPresentVertexShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mPresentPixelShader = nullptr;
        arda::FArdaComputePipelineStateInitializer
            mGenerateGeometryPipelineInitializer;
        arda::FArdaComputePipelineStateInitializer
            mAccumulatePipelineInitializer;
        arda::FArdaGraphicsPipelineStateInitializer
            mPresentPipelineInitializer;
        arda::FArdaRHIRayTracingPipelineRef mRayTracingPipeline;
        arda::FArdaRHIShaderTableRef mShaderTable;

        arda::FArdaRHIBufferRef mVertexBuffer;
        arda::FArdaRHIBufferRef mIndexBuffer;
        arda::FArdaRHIBufferRef mMaterialBuffer;
        arda::FArdaRHIAccelStructRef mBlas;
        arda::FArdaRHIAccelStructRef mTlas;
        arda::FArdaRHITextureRef mAccumulationTexture;

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
