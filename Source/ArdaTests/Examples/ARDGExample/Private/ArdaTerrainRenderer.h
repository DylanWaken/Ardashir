#pragma once

#include "ArdaBackend.h"
#include "ArdaSwapChain.h"
#include "ArdaRenderGraph.h"
#include "PipelineStateCache/ArdaPipelineStateCache.h"

#include <filesystem>
#include <EASTL/string.h>
#include <memory>

namespace arda
{
    class FArdaTerrainRenderer
    {
    public:
        bool Initialize(
            arda::FArdaRHIDeviceRef device,
            arda::EArdaRHIFormat swapChainFormat);
        void UpdateCamera(
            float forward,
            float right,
            float lookX,
            float lookY,
            float deltaSeconds);
        bool RenderFrame(arda::IArdaSwapChain& swapChain);

        [[nodiscard]] const eastl::string& GetError() const { return mError; }

    private:
        bool CreateShadersAndInitializers();
        bool CreateSettingsUploadBuffer();
        bool CreateCameraResources();

        [[nodiscard]] arda::FARDGRenderGraphContext
        CreateGraphContext() const;

        arda::FArdaRHIDeviceRef mDevice;
        arda::FArdaGlobalShaderMap mShaderMap;
        std::unique_ptr<arda::FArdaPipelineStateCache> mPipelineStateCache;
        const arda::FArdaGlobalShaderInstance* mGenerateShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mErodeShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mTriangulateShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mTerrainVertexShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mTerrainPixelShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mOverlayVertexShader = nullptr;
        const arda::FArdaGlobalShaderInstance* mOverlayPixelShader = nullptr;
        arda::FArdaComputePipelineStateInitializer mGeneratePipelineInitializer;
        arda::FArdaComputePipelineStateInitializer mErodePipelineInitializer;
        arda::FArdaComputePipelineStateInitializer mTriangulatePipelineInitializer;
        arda::FArdaRHIInputLayoutRef mTerrainInputLayout;
        arda::FArdaGraphicsPipelineStateInitializer mTerrainPipelineInitializer;
        arda::FArdaGraphicsPipelineStateInitializer mOverlayPipelineInitializer;
        arda::FArdaRHIBufferRef mSettingsUploadBuffer;
        arda::FArdaRHIBufferRef mCameraBuffer;
        arda::FArdaRHIBindingSetRef mCameraBindingSet;

        float mCameraPosition[3] = {-0.96875f, -0.96875f, 0.8125f};
        float mCameraYaw = 0.78539816f;
        float mCameraPitch = -0.67453292f;
        float mElapsedSeconds = 0.0f;
        bool mbTerrainReadbackValidated = false;

        eastl::string mError;
    };
}
