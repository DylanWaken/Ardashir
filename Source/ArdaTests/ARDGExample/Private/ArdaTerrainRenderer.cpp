#include "ArdaARDGExamplePch.h"

#include "ArdaTerrainRenderer.h"

#include <cmath>
#include <memory>
#include <mutex>

namespace arda::tests::ardg_example
{
    namespace
    {
        constexpr uint32_t HeightmapWidth = 128 * 5;
        constexpr uint32_t HeightmapHeight = 128 * 5;
        constexpr uint32_t CellCount = (HeightmapWidth - 1) * (HeightmapHeight - 1);
        constexpr uint32_t TerrainVertexCount = CellCount * 4;
        constexpr uint32_t TerrainIndexCount = CellCount * 6;

        struct alignas(16) FTerrainSettings
        {
            uint32_t mWidth = HeightmapWidth;
            uint32_t mHeight = HeightmapHeight;
            float mFrequency = 3.25f;
            float mAmplitude = 0.95f;
            float mTime = 0.0f;
            float mPadding[3] = {};
        };

        struct FTerrainVertex
        {
            float mPosition[3];
            float mHeight;
        };

        struct alignas(16) FCameraSettings
        {
            float mWorldToView[16];
            float mProjection[16];
        };

        class FGenerateTerrainShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
                ARDA_SHADER_BUFFER_SRV(mSettings, 0, 0, rhi::EArdaRHIShaderStage::Compute)
                ARDA_SHADER_TEXTURE_UAV(mHeightmap, 0, 0, rhi::EArdaRHIShaderStage::Compute)
            ARDA_END_SHADER_PARAMETER_STRUCT()
            ARDA_DECLARE_GLOBAL_SHADER(FGenerateTerrainShader);
        };

        class FErodeTerrainShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
                ARDA_SHADER_TEXTURE_UAV(mHeightmap, 0, 0, rhi::EArdaRHIShaderStage::Compute)
            ARDA_END_SHADER_PARAMETER_STRUCT()
            ARDA_DECLARE_GLOBAL_SHADER(FErodeTerrainShader);
        };

        class FTriangulateTerrainShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
                ARDA_SHADER_TEXTURE_SRV(mHeightmap, 0, 0, rhi::EArdaRHIShaderStage::Compute)
                ARDA_SHADER_BUFFER_UAV(mTerrainVertices, 0, 0, rhi::EArdaRHIShaderStage::Compute)
                ARDA_SHADER_BUFFER_UAV(mTerrainIndices, 1, 0, rhi::EArdaRHIShaderStage::Compute)
            ARDA_END_SHADER_PARAMETER_STRUCT()
            ARDA_DECLARE_GLOBAL_SHADER(FTriangulateTerrainShader);
        };

        class FTerrainVertexShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
                ARDA_SHADER_CONSTANT_BUFFER(mCamera, 0, 0, rhi::EArdaRHIShaderStage::Vertex)
            ARDA_END_SHADER_PARAMETER_STRUCT()
            ARDA_DECLARE_GLOBAL_SHADER(FTerrainVertexShader);
        };

        class FTerrainPixelShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
                ARDA_SHADER_TEXTURE_SRV(mHeightmap, 0, 0, rhi::EArdaRHIShaderStage::Pixel)
            ARDA_END_SHADER_PARAMETER_STRUCT()
            ARDA_DECLARE_GLOBAL_SHADER(FTerrainPixelShader);
        };

        class FTerrainOverlayVertexShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_DECLARE_GLOBAL_SHADER(FTerrainOverlayVertexShader);
        };

        class FTerrainOverlayPixelShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_DECLARE_GLOBAL_SHADER(FTerrainOverlayPixelShader);
        };

        constexpr uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
        {
            return (value + divisor - 1) / divisor;
        }

        ARDG_BEGIN_PARAMETER_STRUCT(FInitializeTerrainSettingsParameters)
            ARDG_BUFFER_ACCESS(mDestination)
        ARDG_END_PARAMETER_STRUCT()
        ARDG_BEGIN_PARAMETER_STRUCT(FUploadTerrainSettingsParameters)
            ARDG_BUFFER_ACCESS(mSource)
            ARDG_BUFFER_ACCESS(mDestination)
        ARDG_END_PARAMETER_STRUCT()
        ARDG_BEGIN_PARAMETER_STRUCT(FGenerateNoiseHeightmapParameters)
            ARDG_BUFFER_SRV(mSettings)
            ARDG_TEXTURE_UAV(mHeightmap)
        ARDG_END_PARAMETER_STRUCT()
        ARDG_BEGIN_PARAMETER_STRUCT(FDebugHeightmapParameters)
            ARDG_TEXTURE_SRV(mHeightmap)
        ARDG_END_PARAMETER_STRUCT()
        ARDG_BEGIN_PARAMETER_STRUCT(FErodeHeightmapParameters)
            ARDG_TEXTURE_UAV(mHeightmap)
        ARDG_END_PARAMETER_STRUCT()
        ARDG_BEGIN_PARAMETER_STRUCT(FTriangulateTerrainParameters)
            ARDG_TEXTURE_SRV(mHeightmap)
            ARDG_BUFFER_UAV(mTerrainVertices)
            ARDG_BUFFER_UAV(mTerrainIndices)
        ARDG_END_PARAMETER_STRUCT()
        ARDG_BEGIN_PARAMETER_STRUCT(FUpdateTerrainCameraParameters)
            ARDG_BUFFER_ACCESS(mDestination)
        ARDG_END_PARAMETER_STRUCT()
        ARDG_BEGIN_PARAMETER_STRUCT(FRenderTerrainParameters)
            ARDG_BUFFER_ACCESS(mTerrainVertices)
            ARDG_BUFFER_ACCESS(mTerrainIndices)
            ARDG_BUFFER_ACCESS(mCamera)
            ARDG_TEXTURE_SRV(mHeightmap)
            ARDG_RENDER_TARGET_BINDING_SLOTS(mTargets)
        ARDG_END_PARAMETER_STRUCT()
        ARDG_BEGIN_PARAMETER_STRUCT(FTerrainOverlayParameters)
            ARDG_RENDER_TARGET_BINDING_SLOTS(mTargets)
        ARDG_END_PARAMETER_STRUCT()

        template <typename T>
        bool TakeResult(rhi::TArdaRHIResult<T>& Result, T& Output, eastl::string& Error)
        {
            if (!Result)
            {
                Error = Result.mStatus.mMessage;
                return false;
            }
            Output = eastl::move(Result.mValue);
            return true;
        }

        class FFrameExecutionErrors final
        {
        public:
            void Record(const char* Operation, const eastl::string& Detail)
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                if (!mFirstError.empty())
                    return;
                mFirstError = Operation ? Operation : "Terrain pass";
                if (!Detail.empty())
                {
                    mFirstError += ": ";
                    mFirstError += Detail;
                }
            }

            [[nodiscard]] eastl::string GetFirstError() const
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                return mFirstError;
            }

        private:
            mutable std::mutex mMutex;
            eastl::string mFirstError;
        };

        eastl::string ValidateTerrainReadback(
            const eastl::vector<uint8_t>& VertexBytes,
            const eastl::vector<uint8_t>& IndexBytes)
        {
            if (VertexBytes.size() !=
                    static_cast<size_t>(TerrainVertexCount) * sizeof(FTerrainVertex) ||
                IndexBytes.size() !=
                    static_cast<size_t>(TerrainIndexCount) * sizeof(uint32_t))
            {
                char Message[256]{};
                std::snprintf(
                    Message, sizeof(Message),
                    "Terrain GPU readback returned unexpected byte counts: vertices expected %zu, got %zu; indices expected %zu, got %zu.",
                    static_cast<size_t>(TerrainVertexCount) * sizeof(FTerrainVertex),
                    VertexBytes.size(),
                    static_cast<size_t>(TerrainIndexCount) * sizeof(uint32_t),
                    IndexBytes.size());
                return Message;
            }

            const auto* Vertices = reinterpret_cast<const FTerrainVertex*>(
                VertexBytes.data());
            const auto* Indices = reinterpret_cast<const uint32_t*>(
                IndexBytes.data());
            constexpr uint32_t LocalIndices[6] = { 0, 2, 1, 1, 2, 3 };
            float MaximumGradient = 0.0f;
            uint32_t MaximumGradientCell = 0;
            for (uint32_t Cell = 0; Cell < CellCount; ++Cell)
            {
                const uint32_t VertexBase = Cell * 4;
                const uint32_t IndexBase = Cell * 6;
                for (uint32_t Index = 0; Index < 6; ++Index)
                {
                    const uint32_t Expected = VertexBase + LocalIndices[Index];
                    if (Indices[IndexBase + Index] != Expected)
                    {
                        char Message[192]{};
                        std::snprintf(
                            Message, sizeof(Message),
                            "Terrain index readback diverged at cell %u index %u: expected %u, got %u.",
                            Cell, Index, Expected, Indices[IndexBase + Index]);
                        return Message;
                    }
                }

                const uint32_t CellX = Cell % (HeightmapWidth - 1);
                const uint32_t CellY = Cell / (HeightmapWidth - 1);
                constexpr uint32_t CornerX[4] = { 0, 1, 0, 1 };
                constexpr uint32_t CornerY[4] = { 0, 0, 1, 1 };
                for (uint32_t Corner = 0; Corner < 4; ++Corner)
                {
                    const FTerrainVertex& Vertex = Vertices[VertexBase + Corner];
                    const float ExpectedX =
                        ((static_cast<float>(CellY + CornerY[Corner]) /
                            static_cast<float>(HeightmapHeight - 1)) - 0.5f) * 1.45f;
                    const float ExpectedY =
                        ((static_cast<float>(CellX + CornerX[Corner]) /
                            static_cast<float>(HeightmapWidth - 1)) - 0.5f) * 1.45f;
                    if (!std::isfinite(Vertex.mPosition[0]) ||
                        !std::isfinite(Vertex.mPosition[1]) ||
                        !std::isfinite(Vertex.mPosition[2]) ||
                        !std::isfinite(Vertex.mHeight) ||
                        std::abs(Vertex.mPosition[0] - ExpectedX) > 0.00001f ||
                        std::abs(Vertex.mPosition[1] - ExpectedY) > 0.00001f ||
                        std::abs(Vertex.mPosition[2] -
                            (Vertex.mHeight * 0.72f - 0.32f)) > 0.00002f)
                    {
                        char Message[192]{};
                        std::snprintf(
                            Message, sizeof(Message),
                            "Terrain vertex readback diverged at cell %u corner %u: position=(%.6f, %.6f, %.6f), height=%.6f.",
                            Cell, Corner,
                            Vertex.mPosition[0], Vertex.mPosition[1],
                            Vertex.mPosition[2], Vertex.mHeight);
                        return Message;
                    }
                }

                const auto HeightDiff = [](float Left, float Right)
                {
                    return std::abs(Left - Right);
                };
                const float HorizontalGradient = HeightDiff(
                    Vertices[VertexBase + 0].mHeight,
                    Vertices[VertexBase + 1].mHeight);
                const float VerticalGradient = HeightDiff(
                    Vertices[VertexBase + 0].mHeight,
                    Vertices[VertexBase + 2].mHeight);
                const float Gradient = eastl::max(
                    HorizontalGradient, VerticalGradient);
                if (Gradient > MaximumGradient)
                {
                    MaximumGradient = Gradient;
                    MaximumGradientCell = Cell;
                }
                if (CellX + 1 < HeightmapWidth - 1)
                {
                    const FTerrainVertex* Right =
                        Vertices + VertexBase + 4;
                    if (HeightDiff(Vertices[VertexBase + 1].mHeight,
                            Right[0].mHeight) > 0.000001f ||
                        HeightDiff(Vertices[VertexBase + 3].mHeight,
                            Right[2].mHeight) > 0.000001f)
                    {
                        char Message[160]{};
                        std::snprintf(Message, sizeof(Message),
                            "Terrain readback has a horizontal height seam after cell %u.",
                            Cell);
                        return Message;
                    }
                }
                if (CellY + 1 < HeightmapHeight - 1)
                {
                    const FTerrainVertex* Below = Vertices +
                        VertexBase + (HeightmapWidth - 1) * 4;
                    if (HeightDiff(Vertices[VertexBase + 2].mHeight,
                            Below[0].mHeight) > 0.000001f ||
                        HeightDiff(Vertices[VertexBase + 3].mHeight,
                            Below[1].mHeight) > 0.000001f)
                    {
                        char Message[160]{};
                        std::snprintf(Message, sizeof(Message),
                            "Terrain readback has a vertical height seam after cell %u.",
                            Cell);
                        return Message;
                    }
                }
            }
            if (MaximumGradient > 0.10f)
            {
                char Message[160]{};
                std::snprintf(Message, sizeof(Message),
                    "Terrain readback has a discontinuous height gradient of %.6f at cell %u.",
                    MaximumGradient, MaximumGradientCell);
                return Message;
            }
            return {};
        }
    }

    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FGenerateTerrainShader, "/ArdaTests/ARDGExample/ArdaTerrain.hlsl", "TerrainGenerateCS",
        "GenerateNoiseHeightmapCS", rhi::EArdaRHIShaderStage::Compute)
    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FErodeTerrainShader, "/ArdaTests/ARDGExample/ArdaTerrain.hlsl", "TerrainErodeCS",
        "ErodeHeightmapCS", rhi::EArdaRHIShaderStage::Compute)
    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FTriangulateTerrainShader, "/ArdaTests/ARDGExample/ArdaTerrain.hlsl", "TerrainTriangulateCS",
        "TriangulateTerrainCS", rhi::EArdaRHIShaderStage::Compute)
    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FTerrainVertexShader, "/ArdaTests/ARDGExample/ArdaTerrain.hlsl", "TerrainVS",
        "TerrainVS", rhi::EArdaRHIShaderStage::Vertex)
    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FTerrainPixelShader, "/ArdaTests/ARDGExample/ArdaTerrain.hlsl", "TerrainPS",
        "TerrainPS", rhi::EArdaRHIShaderStage::Pixel)
    ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(
        FTerrainOverlayVertexShader, "/ArdaTests/ARDGExample/ArdaTerrain.hlsl", "TerrainOverlayVS",
        "TerrainOverlayVS", rhi::EArdaRHIShaderStage::Vertex)
    ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(
        FTerrainOverlayPixelShader, "/ArdaTests/ARDGExample/ArdaTerrain.hlsl", "TerrainOverlayPS",
        "TerrainOverlayPS", rhi::EArdaRHIShaderStage::Pixel)

    bool FArdaTerrainRenderer::Initialize(
        rhi::FArdaRHIDeviceRef device,
        rhi::EArdaRHIFormat)
    {
        mDevice = eastl::move(device);
        if (!mDevice || !mDevice->GetCapabilities().mQueues.mbGraphics)
        {
            mError = "The initialized backend does not expose a graphics device.";
            return false;
        }
        mPipelineStateCache =
            std::make_unique<backend::FArdaPipelineStateCache>(mDevice);
        if (!CreateShadersAndInitializers() ||
            !CreateSettingsUploadBuffer() ||
            !CreateCameraResources())
        {
            return false;
        }
        mError.clear();
        return true;
    }

    bool FArdaTerrainRenderer::CreateShadersAndInitializers()
    {
        if (!mShaderMap.Initialize(mDevice))
        {
            const auto& Diagnostics = mShaderMap.GetDiagnostics();
            mError = Diagnostics.empty()
                ? "Global shader map initialization failed."
                : Diagnostics.back().mMessage;
            return false;
        }
        mGenerateShader = mShaderMap.Find(FGenerateTerrainShader::GetStaticType());
        mErodeShader = mShaderMap.Find(FErodeTerrainShader::GetStaticType());
        mTriangulateShader = mShaderMap.Find(FTriangulateTerrainShader::GetStaticType());
        mTerrainVertexShader = mShaderMap.Find(FTerrainVertexShader::GetStaticType());
        mTerrainPixelShader = mShaderMap.Find(FTerrainPixelShader::GetStaticType());
        mOverlayVertexShader = mShaderMap.Find(FTerrainOverlayVertexShader::GetStaticType());
        mOverlayPixelShader = mShaderMap.Find(FTerrainOverlayPixelShader::GetStaticType());
        if (mGenerateShader == nullptr || mErodeShader == nullptr ||
            mTriangulateShader == nullptr || mTerrainVertexShader == nullptr ||
            mTerrainPixelShader == nullptr || mOverlayVertexShader == nullptr ||
            mOverlayPixelShader == nullptr)
        {
            mError = "A registered terrain shader is absent from the global shader map.";
            return false;
        }

        mGeneratePipelineInitializer =
            backend::FArdaComputePipelineStateInitializer::FromGlobalShader(
                *mGenerateShader, "Generate pipeline");
        mErodePipelineInitializer =
            backend::FArdaComputePipelineStateInitializer::FromGlobalShader(
                *mErodeShader, "Erode pipeline");
        mTriangulatePipelineInitializer =
            backend::FArdaComputePipelineStateInitializer::FromGlobalShader(
                *mTriangulateShader, "Triangulate pipeline");

        eastl::vector<rhi::FArdaRHIVertexAttributeDesc> attributes(2);
        attributes[0].mSemanticName = "POSITION";
        attributes[0].mFormat = rhi::EArdaRHIFormat::RGB32Float;
        attributes[0].mOffset = offsetof(FTerrainVertex, mPosition);
        attributes[0].mElementStride = sizeof(FTerrainVertex);
        attributes[1].mSemanticName = "HEIGHT";
        attributes[1].mFormat = rhi::EArdaRHIFormat::R32Float;
        attributes[1].mOffset = offsetof(FTerrainVertex, mHeight);
        attributes[1].mElementStride = sizeof(FTerrainVertex);
        auto inputLayout = mDevice->CreateInputLayout(
            attributes, mTerrainVertexShader->GetShader());
        if (!TakeResult(inputLayout, mTerrainInputLayout, mError))
        {
            return false;
        }

        rhi::FArdaRHIGraphicsPipelineDesc terrainFixedState;
        terrainFixedState.mRasterState.mCullMode = rhi::EArdaRHICullMode::None;
        terrainFixedState.mDepthStencilState.mDepthFunc =
            rhi::EArdaRHIComparisonFunc::GreaterOrEqual;
        terrainFixedState.mSampleCount = 0;
        terrainFixedState.mDebugName = "Terrain pipeline";
        mTerrainPipelineInitializer =
            backend::FArdaGraphicsPipelineStateInitializer::FromGlobalShaders(
                *mTerrainVertexShader, mTerrainPixelShader,
                mTerrainInputLayout, terrainFixedState);

        rhi::FArdaRHIGraphicsPipelineDesc overlayFixedState;
        overlayFixedState.mRasterState.mCullMode = rhi::EArdaRHICullMode::None;
        overlayFixedState.mDepthStencilState.mbDepthTest = false;
        overlayFixedState.mDepthStencilState.mbDepthWrite = false;
        overlayFixedState.mBlendState.mTargets[0].mbEnable = true;
        overlayFixedState.mBlendState.mTargets[0].mSourceColor =
            rhi::EArdaRHIBlendFactor::SourceAlpha;
        overlayFixedState.mBlendState.mTargets[0].mDestinationColor =
            rhi::EArdaRHIBlendFactor::InverseSourceAlpha;
        overlayFixedState.mSampleCount = 0;
        overlayFixedState.mDebugName = "Terrain overlay pipeline";
        mOverlayPipelineInitializer =
            backend::FArdaGraphicsPipelineStateInitializer::FromGlobalShaders(
                *mOverlayVertexShader, mOverlayPixelShader, {},
                overlayFixedState);
        return true;
    }

    bool FArdaTerrainRenderer::CreateCameraResources()
    {
        rhi::FArdaRHIBufferDesc desc;
        desc.mByteSize = sizeof(FCameraSettings);
        desc.mUsage = rhi::EArdaRHIBufferUsage::Constant;
        desc.mInitialState = rhi::EArdaRHIResourceState::ConstantBuffer;
        desc.mbKeepInitialState = true;
        desc.mDebugName = "Terrain camera";
        auto buffer = mDevice->CreateBuffer(desc);
        if (!TakeResult(buffer, mCameraBuffer, mError))
        {
            return false;
        }
        if (mTerrainVertexShader == nullptr ||
            mTerrainVertexShader->GetBindingLayouts().empty())
        {
            mError = "The registered terrain vertex shader has no camera layout.";
            return false;
        }
        FTerrainVertexShader::FParameters parameters;
        parameters.mCamera = mCameraBuffer;
        const auto status =
            FTerrainVertexShader::FParameters::GetStaticMetadata().CreateBindingSet(
                *mDevice,
                &parameters,
                mTerrainVertexShader->GetBindingLayouts()[0],
                mCameraBindingSet);
        if (!status)
        {
            mError = status.mMessage;
            return false;
        }
        return true;
    }

    bool FArdaTerrainRenderer::CreateSettingsUploadBuffer()
    {
        rhi::FArdaRHIBufferDesc desc;
        desc.mByteSize = sizeof(FTerrainSettings);
        desc.mStructureStride = sizeof(FTerrainSettings);
        desc.mInitialState = rhi::EArdaRHIResourceState::CopySource;
        desc.mbKeepInitialState = true;
        desc.mDebugName = "Terrain settings upload";
        auto buffer = mDevice->CreateBuffer(desc);
        return TakeResult(buffer, mSettingsUploadBuffer, mError);
    }

    void FArdaTerrainRenderer::UpdateCamera(
        float forward, float right, float lookX, float lookY, float deltaSeconds)
    {
        constexpr float LookSensitivity = 0.0025f;
        constexpr float MoveSpeed = 1.1f;
        constexpr float PitchLimit = 1.50f;
        mElapsedSeconds += eastl::min(deltaSeconds, 0.1f);
        mCameraYaw += lookX * LookSensitivity;
        mCameraPitch = eastl::clamp(
            mCameraPitch - lookY * LookSensitivity, -PitchLimit, PitchLimit);
        const float cosPitch = std::cos(mCameraPitch);
        const float forwardVector[3] = {
            std::cos(mCameraYaw) * cosPitch,
            std::sin(mCameraYaw) * cosPitch,
            std::sin(mCameraPitch)};
        const float rightVector[3] = {
            -std::sin(mCameraYaw), std::cos(mCameraYaw), 0.0f};
        const float inputLength = std::sqrt(forward * forward + right * right);
        if (inputLength > 1.0f)
        {
            forward /= inputLength;
            right /= inputLength;
        }
        const float distance = MoveSpeed * eastl::min(deltaSeconds, 0.1f);
        for (uint32_t component = 0; component < 3; ++component)
        {
            mCameraPosition[component] +=
                (forwardVector[component] * forward + rightVector[component] * right) * distance;
        }
    }

    bool FArdaTerrainRenderer::RenderFrame(backend::IArdaSwapChain& swapChain)
    {
        rhi::FArdaRHIFramebufferRef framebuffer;
        if (!swapChain.AcquireFrame(framebuffer))
        {
            mError = swapChain.GetError();
            return false;
        }
        const auto& framebufferDesc = framebuffer->GetDesc();
        if (framebufferDesc.mColorAttachments.empty() ||
            !framebufferDesc.mColorAttachments[0].mTexture)
        {
            mError = "The acquired swap-chain framebuffer has no color attachment.";
            return false;
        }
        const auto colorAttachment = framebufferDesc.mColorAttachments[0];
        auto executionErrors = std::make_shared<FFrameExecutionErrors>();
        eastl::vector<uint8_t> terrainVertexReadback;
        eastl::vector<uint8_t> terrainIndexReadback;
        render_graph::FARDGBuilder graph(CreateGraphContext());
        auto* backBuffer = graph.RegisterExternalTexture(
            colorAttachment.mTexture, rhi::EArdaRHIResourceState::Present, "BackBuffer");
        auto* settingsUpload = graph.RegisterExternalBuffer(
            mSettingsUploadBuffer, rhi::EArdaRHIResourceState::CopySource, "TerrainSettingsUpload");
        auto* cameraBuffer = graph.RegisterExternalBuffer(
            mCameraBuffer, rhi::EArdaRHIResourceState::ConstantBuffer, "TerrainCamera");

        rhi::FArdaRHIBufferDesc settingsDesc;
        settingsDesc.mDebugName = "TerrainSettings";
        settingsDesc.mByteSize = sizeof(FTerrainSettings);
        settingsDesc.mStructureStride = sizeof(FTerrainSettings);
        settingsDesc.mUsage = rhi::EArdaRHIBufferUsage::Structured | rhi::EArdaRHIBufferUsage::ShaderResource;
        auto* terrainSettings = graph.CreateBuffer(settingsDesc);

        rhi::FArdaRHITextureDesc heightmapDesc;
        heightmapDesc.mDebugName = "Heightmap";
        heightmapDesc.mWidth = HeightmapWidth;
        heightmapDesc.mHeight = HeightmapHeight;
        heightmapDesc.mMipLevels = 2;
        heightmapDesc.mFormat = rhi::EArdaRHIFormat::R32Float;
        heightmapDesc.mUsage = rhi::EArdaRHITextureUsage::ShaderResource |
            rhi::EArdaRHITextureUsage::UnorderedAccess;
        auto* heightmap = graph.CreateTexture(heightmapDesc);

        rhi::FArdaRHIBufferDesc vertexDesc;
        vertexDesc.mDebugName = "TerrainVertices";
        vertexDesc.mByteSize = TerrainVertexCount * sizeof(FTerrainVertex);
        vertexDesc.mStructureStride = sizeof(FTerrainVertex);
        vertexDesc.mUsage = rhi::EArdaRHIBufferUsage::Structured |
            rhi::EArdaRHIBufferUsage::UnorderedAccess | rhi::EArdaRHIBufferUsage::Vertex;
        auto* terrainVertices = graph.CreateBuffer(vertexDesc);
        rhi::FArdaRHIBufferDesc indexDesc;
        indexDesc.mDebugName = "TerrainIndices";
        indexDesc.mByteSize = TerrainIndexCount * sizeof(uint32_t);
        indexDesc.mStructureStride = sizeof(uint32_t);
        indexDesc.mUsage = rhi::EArdaRHIBufferUsage::Structured |
            rhi::EArdaRHIBufferUsage::UnorderedAccess | rhi::EArdaRHIBufferUsage::Index;
        auto* terrainIndices = graph.CreateBuffer(indexDesc);

        rhi::FArdaRHITextureDesc depthDesc;
        depthDesc.mDebugName = "TerrainDepth";
        depthDesc.mWidth = swapChain.GetWidth();
        depthDesc.mHeight = swapChain.GetHeight();
        depthDesc.mFormat = rhi::EArdaRHIFormat::D32;
        depthDesc.mUsage = rhi::EArdaRHITextureUsage::DepthStencil;
        auto* terrainDepth = graph.CreateTexture(depthDesc);

        render_graph::FARDGTextureViewDesc heightmapView;
        heightmapView.mTexture = heightmap->GetHandle();
        heightmapView.mSubresources = { 0, 1, 0, 1 };
        auto* heightmapUAV = graph.CreateTextureUAV("Heightmap mip 0 UAV", heightmapView);
        auto* heightmapSRV = graph.CreateTextureSRV("Heightmap mip 0 SRV", heightmapView);
        render_graph::FARDGBufferViewDesc settingsView;
        settingsView.mBuffer = terrainSettings->GetHandle();
        auto* terrainSettingsSRV = graph.CreateBufferSRV("TerrainSettings SRV", settingsView);
        render_graph::FARDGBufferViewDesc vertexView;
        vertexView.mBuffer = terrainVertices->GetHandle();
        auto* terrainVerticesUAV = graph.CreateBufferUAV("TerrainVertices UAV", vertexView);
        render_graph::FARDGBufferViewDesc indexView;
        indexView.mBuffer = terrainIndices->GetHandle();
        auto* terrainIndicesUAV = graph.CreateBufferUAV("TerrainIndices UAV", indexView);

        FTerrainSettings settingsData;
        settingsData.mTime = mElapsedSeconds;
        FInitializeTerrainSettingsParameters updateSettings;
        updateSettings.mDestination = {
            settingsUpload, rhi::EArdaRHIResourceState::CopyDest, {}};
        (void)graph.AddPass(
            "UpdateTerrainSettings", &updateSettings, render_graph::EARDGPassFlags::None,
            [settingsData](render_graph::FARDGPassExecutionContext& context,
                           const FInitializeTerrainSettingsParameters& frozen)
            {
                (void)context.mCommandList.WriteBuffer(
                    *context.GetBuffer(frozen.mDestination.mBuffer),
                    &settingsData, sizeof(settingsData));
            });

        FUploadTerrainSettingsParameters upload;
        upload.mSource = { settingsUpload, rhi::EArdaRHIResourceState::CopySource, {} };
        upload.mDestination = { terrainSettings, rhi::EArdaRHIResourceState::CopyDest, {} };
        (void)graph.AddPass(
            "UploadTerrainSettings", &upload, render_graph::EARDGPassFlags::Copy,
            [](render_graph::FARDGPassExecutionContext& context,
               const FUploadTerrainSettingsParameters& frozen)
            {
                (void)context.mCommandList.CopyBuffer(
                    *context.GetBuffer(frozen.mDestination.mBuffer), 0,
                    *context.GetBuffer(frozen.mSource.mBuffer), 0, sizeof(FTerrainSettings));
            });

        FGenerateNoiseHeightmapParameters generate;
        generate.mSettings = terrainSettingsSRV;
        generate.mHeightmap = heightmapUAV;
        (void)graph.AddDispatchPass(
            "GenerateNoiseHeightmap", &generate,
            { DivideRoundUp(HeightmapWidth, 8), DivideRoundUp(HeightmapHeight, 8), 1 },
            [this, executionErrors](render_graph::FARDGPassExecutionContext& context)
            {
                rhi::FArdaRHIComputeState state;
                state.mBindings.push_back(context.CreateBindingSet(*mGenerateShader));
                const auto status = mPipelineStateCache->SetComputePipelineState(
                    context.mCommandList, mGeneratePipelineInitializer,
                    eastl::move(state));
                if (!status)
                {
                    executionErrors->Record(
                        "GenerateNoiseHeightmap PSO bind failed",
                        status.mMessage);
                    return;
                }
            },
            render_graph::EARDGPassFlags::AsyncCompute);

        FDebugHeightmapParameters debugHeightmap;
        debugHeightmap.mHeightmap = heightmapSRV;
        (void)graph.AddPass(
            "DebugHeightmap", &debugHeightmap, render_graph::EARDGPassFlags::None,
            [](const FDebugHeightmapParameters&) {});

        FErodeHeightmapParameters erode;
        erode.mHeightmap = heightmapUAV;
        (void)graph.AddDispatchPass(
            "ErodeHeightmap", &erode,
            { DivideRoundUp(HeightmapWidth, 8), DivideRoundUp(HeightmapHeight, 8), 1 },
            [this, executionErrors](render_graph::FARDGPassExecutionContext& context)
            {
                rhi::FArdaRHIComputeState state;
                state.mBindings.push_back(context.CreateBindingSet(*mErodeShader));
                const auto status = mPipelineStateCache->SetComputePipelineState(
                    context.mCommandList, mErodePipelineInitializer,
                    eastl::move(state));
                if (!status)
                {
                    executionErrors->Record(
                        "ErodeHeightmap PSO bind failed", status.mMessage);
                    return;
                }
            },
            render_graph::EARDGPassFlags::AsyncCompute);

        FTriangulateTerrainParameters triangulate;
        triangulate.mHeightmap = heightmapSRV;
        triangulate.mTerrainVertices = terrainVerticesUAV;
        triangulate.mTerrainIndices = terrainIndicesUAV;
        (void)graph.AddDispatchPass(
            "TriangulateTerrain", &triangulate,
            { DivideRoundUp(HeightmapWidth - 1, 8), DivideRoundUp(HeightmapHeight - 1, 8), 1 },
            [this, executionErrors](render_graph::FARDGPassExecutionContext& context)
            {
                rhi::FArdaRHIComputeState state;
                state.mBindings.push_back(context.CreateBindingSet(*mTriangulateShader));
                const auto status = mPipelineStateCache->SetComputePipelineState(
                    context.mCommandList, mTriangulatePipelineInitializer,
                    eastl::move(state));
                if (!status)
                {
                    executionErrors->Record(
                        "TriangulateTerrain PSO bind failed",
                        status.mMessage);
                    return;
                }
            },
            render_graph::EARDGPassFlags::AsyncCompute);

        if (!mbTerrainReadbackValidated)
        {
            (void)graph.AddDeviceToHostCopyPass(
                terrainVertices, terrainVertexReadback,
                0, rhi::ArdaRHIWholeBuffer,
                "ReadbackTerrainVertices");
            (void)graph.AddDeviceToHostCopyPass(
                terrainIndices, terrainIndexReadback,
                0, rhi::ArdaRHIWholeBuffer,
                "ReadbackTerrainIndices");
        }

        FRenderTerrainParameters render;
        render.mTerrainVertices = { terrainVertices, rhi::EArdaRHIResourceState::VertexBuffer, {} };
        render.mTerrainIndices = { terrainIndices, rhi::EArdaRHIResourceState::IndexBuffer, {} };
        render.mCamera = { cameraBuffer, rhi::EArdaRHIResourceState::ConstantBuffer, {} };
        render.mHeightmap = heightmapSRV;
        render.mTargets.mColor[0] = { backBuffer, colorAttachment.mAttachment.mSubresources };
        render.mTargets.mDepthStencil = { terrainDepth, {} };

        const uint32_t width = swapChain.GetWidth();
        const uint32_t height = swapChain.GetHeight();
        const float cosPitch = std::cos(mCameraPitch);
        const float forward[3] = {
            std::cos(mCameraYaw) * cosPitch,
            std::sin(mCameraYaw) * cosPitch,
            std::sin(mCameraPitch)};
        const float right[3] = {-std::sin(mCameraYaw), std::cos(mCameraYaw), 0.0f};
        const float up[3] = {
            forward[1] * right[2] - forward[2] * right[1],
            forward[2] * right[0] - forward[0] * right[2],
            forward[0] * right[1] - forward[1] * right[0]};
        const float viewTranslation[3] = {
            -(mCameraPosition[0] * right[0] + mCameraPosition[1] * right[1] + mCameraPosition[2] * right[2]),
            -(mCameraPosition[0] * up[0] + mCameraPosition[1] * up[1] + mCameraPosition[2] * up[2]),
            -(mCameraPosition[0] * forward[0] + mCameraPosition[1] * forward[1] + mCameraPosition[2] * forward[2])};
        constexpr float NearPlane = 0.05f;
        constexpr float HorizontalHalfFov = 0.78539816f;
        const float xScale = 1.0f / std::tan(HorizontalHalfFov);
        const float yScale = xScale * static_cast<float>(width) / static_cast<float>(height);
        const FCameraSettings cameraSettings = {
            {
                right[0], up[0], forward[0], 0.0f,
                right[1], up[1], forward[1], 0.0f,
                right[2], up[2], forward[2], 0.0f,
                viewTranslation[0], viewTranslation[1], viewTranslation[2], 1.0f
            },
            {
                xScale, 0.0f, 0.0f, 0.0f,
                0.0f, yScale, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, NearPlane, 0.0f
            }};

        FUpdateTerrainCameraParameters updateCamera;
        updateCamera.mDestination = {
            cameraBuffer, rhi::EArdaRHIResourceState::CopyDest, {}};
        (void)graph.AddPass(
            "UpdateTerrainCamera",
            &updateCamera,
            render_graph::EARDGPassFlags::None,
            [cameraSettings](
                render_graph::FARDGPassExecutionContext& context,
                const FUpdateTerrainCameraParameters& frozen)
            {
                (void)context.mCommandList.WriteBuffer(
                    *context.GetBuffer(frozen.mDestination.mBuffer),
                    &cameraSettings,
                    sizeof(cameraSettings));
            });

        (void)graph.AddPass(
            "RenderTerrain", &render, render_graph::EARDGPassFlags::Raster,
            [this, width, height, executionErrors](
                render_graph::FARDGPassExecutionContext& context,
                const FRenderTerrainParameters& frozen)
            {
                auto* color = context.GetTexture(frozen.mTargets.mColor[0].mTexture);
                auto* depth = context.GetTexture(frozen.mTargets.mDepthStencil.mTexture);
                rhi::FArdaRHIFramebufferDesc framebufferDesc;
                framebufferDesc.mColorAttachments.push_back({
                    rhi::FArdaRHITextureRef(color),
                    { frozen.mTargets.mColor[0].mSubresources }});
                framebufferDesc.mDepthAttachment = {
                    rhi::FArdaRHITextureRef(depth),
                    { frozen.mTargets.mDepthStencil.mSubresources }};
                auto terrainFramebuffer = mDevice->CreateFramebuffer(framebufferDesc);
                if (!terrainFramebuffer)
                {
                    executionErrors->Record(
                        "RenderTerrain framebuffer creation failed",
                        terrainFramebuffer.mStatus.mMessage);
                    return;
                }
                (void)context.mCommandList.ClearTexture(
                    *color, frozen.mTargets.mColor[0].mSubresources,
                    { 0.004f, 0.007f, 0.009f, 1.0f });
                (void)context.mCommandList.ClearDepthStencilTexture(
                    *depth, frozen.mTargets.mDepthStencil.mSubresources,
                    true, 0.0f, false, 0);
                rhi::FArdaRHIGraphicsState state;
                state.mFramebuffer = terrainFramebuffer.mValue;
                state.mBindings = {
                    mCameraBindingSet,
                    context.CreateBindingSet(*mTerrainPixelShader)};
                state.mVertexBuffers.push_back({
                    rhi::FArdaRHIBufferRef(context.GetBuffer(frozen.mTerrainVertices.mBuffer)), 0, 0});
                state.mIndexBuffer.Reset(context.GetBuffer(frozen.mTerrainIndices.mBuffer));
                state.mIndexFormat = rhi::EArdaRHIFormat::R32UInt;
                state.mViewports.push_back({
                    0.f, static_cast<float>(width), 0.f, static_cast<float>(height), 0.f, 1.f});
                state.mScissors.push_back({
                    0, static_cast<int32_t>(width), 0, static_cast<int32_t>(height)});
                const auto status = mPipelineStateCache->SetGraphicsPipelineState(
                    context.mCommandList, mTerrainPipelineInitializer,
                    eastl::move(state));
                if (!status)
                {
                    executionErrors->Record(
                        "RenderTerrain PSO bind failed", status.mMessage);
                    return;
                }
                context.mCommandList.DrawIndexed({ TerrainIndexCount });
            });

        FTerrainOverlayParameters overlay;
        overlay.mTargets.mColor[0] = {
            backBuffer, colorAttachment.mAttachment.mSubresources };
        (void)graph.AddPass(
            "TerrainOverlay", &overlay, render_graph::EARDGPassFlags::Raster,
            [this, framebuffer, width, height, executionErrors](
                render_graph::FARDGPassExecutionContext& context,
                const FTerrainOverlayParameters& frozen)
            {
                (void)context.GetTexture(frozen.mTargets.mColor[0].mTexture);
                rhi::FArdaRHIGraphicsState state;
                state.mFramebuffer = framebuffer;
                state.mViewports.push_back({
                    0.f, static_cast<float>(width), 0.f, static_cast<float>(height), 0.f, 1.f});
                state.mScissors.push_back({
                    0, static_cast<int32_t>(width), 0, static_cast<int32_t>(height)});
                const auto status = mPipelineStateCache->SetGraphicsPipelineState(
                    context.mCommandList, mOverlayPipelineInitializer,
                    eastl::move(state));
                if (!status)
                {
                    executionErrors->Record(
                        "TerrainOverlay PSO bind failed", status.mMessage);
                    return;
                }
                context.mCommandList.Draw({ 3 });
            });

        swapChain.PrepareSubmit();
        const auto& executionResult = graph.Execute();
        mError = executionErrors->GetFirstError();
        if (!mError.empty())
            return false;
        if (!executionResult.mStatus)
        {
            mError = "Terrain render graph execution failed: ";
            mError += executionResult.mStatus.mMessage;
            return false;
        }
        if (!mbTerrainReadbackValidated)
        {
            mError = ValidateTerrainReadback(
                terrainVertexReadback, terrainIndexReadback);
            if (!mError.empty())
                return false;
            mbTerrainReadbackValidated = true;
        }
        if (executionResult.mSubmittedCommandListCount == 0)
        {
            mError = "Terrain render graph submitted no command lists.";
            return false;
        }
        if (!swapChain.Present())
        {
            mError = swapChain.GetError();
            return false;
        }
        mError.clear();
        return true;
    }

    render_graph::FARDGRenderGraphContext FArdaTerrainRenderer::CreateGraphContext() const
    {
        return render_graph::MakeRenderGraphContext(mDevice);
    }

}
