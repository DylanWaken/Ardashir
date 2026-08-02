#include "ArdaARDGExamplePch.h"

#include "ArdaTerrainRenderer.h"

#include <cmath>

namespace arda::tests::ardg_example
{
    namespace
    {
        constexpr uint32_t HeightmapWidth = 128 * 5;
        constexpr uint32_t HeightmapHeight = 128 * 5;
        constexpr uint32_t CellCount =
            (HeightmapWidth - 1) * (HeightmapHeight - 1);
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
            ARDG_BUFFER_ACCESS(mSettings)
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
    }

    bool FArdaTerrainRenderer::Initialize(
        const backend::FArdaDeviceContext& deviceContext,
        nvrhi::Format swapChainFormat,
        const std::filesystem::path& shaderDirectory)
    {
        mDevice = deviceContext.mDevice;
        mQueueCapabilities.mbGraphics =
            deviceContext.mQueueCapabilities.mbGraphics;
        mQueueCapabilities.mbCompute =
            deviceContext.mQueueCapabilities.mbCompute;
        mQueueCapabilities.mbCopy =
            deviceContext.mQueueCapabilities.mbCopy;
        if (!mDevice || !mQueueCapabilities.mbGraphics)
        {
            mError = "The initialized backend does not expose a graphics device.";
            return false;
        }

        if (!CreateShadersAndPipelines(
                swapChainFormat,
                shaderDirectory,
                deviceContext.mBackend == backend::EArdaBackendType::Vulkan) ||
            !CreateSettingsUploadBuffer() ||
            !CreateCameraResources())
        {
            return false;
        }

        mError.clear();
        return true;
    }

    bool FArdaTerrainRenderer::CreateShadersAndPipelines(
        nvrhi::Format swapChainFormat,
        const std::filesystem::path& shaderDirectory,
        bool vulkan)
    {
        const char* extension = vulkan ? ".spv" : ".dxil";
        eastl::vector<uint8_t> binary;

        auto createShader =
            [this, &binary, &shaderDirectory, extension](
                const char* fileStem,
                const char* entry,
                nvrhi::ShaderType type,
                const char* debugName,
                nvrhi::ShaderHandle& output) -> bool
        {
            std::filesystem::path path =
                shaderDirectory / (std::string(fileStem) + extension);
            if (!LoadBinary(path, binary, mError))
            {
                return false;
            }

            output = mDevice->createShader(
                nvrhi::ShaderDesc()
                    .setShaderType(type)
                    .setEntryName(entry)
                    .setDebugName(debugName),
                binary.data(),
                binary.size());
            if (!output)
            {
                mError = "NVRHI failed to create shader: ";
                mError += debugName;
                return false;
            }
            return true;
        };

        if (!createShader(
                "TerrainGenerateCS",
                "GenerateNoiseHeightmapCS",
                nvrhi::ShaderType::Compute,
                "Generate noise heightmap",
                mGenerateShader) ||
            !createShader(
                "TerrainErodeCS",
                "ErodeHeightmapCS",
                nvrhi::ShaderType::Compute,
                "Erode heightmap",
                mErodeShader) ||
            !createShader(
                "TerrainTriangulateCS",
                "TriangulateTerrainCS",
                nvrhi::ShaderType::Compute,
                "Triangulate terrain",
                mTriangulateShader) ||
            !createShader(
                "TerrainVS",
                "TerrainVS",
                nvrhi::ShaderType::Vertex,
                "Terrain vertex",
                mTerrainVertexShader) ||
            !createShader(
                "TerrainPS",
                "TerrainPS",
                nvrhi::ShaderType::Pixel,
                "Terrain pixel",
                mTerrainPixelShader) ||
            !createShader(
                "TerrainOverlayVS",
                "TerrainOverlayVS",
                nvrhi::ShaderType::Vertex,
                "Terrain overlay vertex",
                mOverlayVertexShader) ||
            !createShader(
                "TerrainOverlayPS",
                "TerrainOverlayPS",
                nvrhi::ShaderType::Pixel,
                "Terrain overlay pixel",
                mOverlayPixelShader))
        {
            return false;
        }

        mGenerateBindingLayout = mDevice->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(nvrhi::ShaderType::Compute)
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0)));
        mErodeBindingLayout = mDevice->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(nvrhi::ShaderType::Compute)
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0)));
        mTriangulateBindingLayout = mDevice->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(nvrhi::ShaderType::Compute)
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1)));
        mCameraBindingLayout = mDevice->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(nvrhi::ShaderType::Vertex)
                .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0)));
        mTerrainPixelBindingLayout = mDevice->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(nvrhi::ShaderType::Pixel)
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0)));
        if (!mGenerateBindingLayout ||
            !mErodeBindingLayout ||
            !mTriangulateBindingLayout ||
            !mCameraBindingLayout ||
            !mTerrainPixelBindingLayout)
        {
            mError = "NVRHI failed to create terrain binding layouts.";
            return false;
        }

        mGeneratePipeline = mDevice->createComputePipeline(
            nvrhi::ComputePipelineDesc()
                .setComputeShader(mGenerateShader)
                .addBindingLayout(mGenerateBindingLayout));
        mErodePipeline = mDevice->createComputePipeline(
            nvrhi::ComputePipelineDesc()
                .setComputeShader(mErodeShader)
                .addBindingLayout(mErodeBindingLayout));
        mTriangulatePipeline = mDevice->createComputePipeline(
            nvrhi::ComputePipelineDesc()
                .setComputeShader(mTriangulateShader)
                .addBindingLayout(mTriangulateBindingLayout));
        if (!mGeneratePipeline || !mErodePipeline || !mTriangulatePipeline)
        {
            mError = "NVRHI failed to create terrain compute pipelines.";
            return false;
        }

        const nvrhi::VertexAttributeDesc attributes[] = {
            nvrhi::VertexAttributeDesc()
                .setName("POSITION")
                .setFormat(nvrhi::Format::RGB32_FLOAT)
                .setBufferIndex(0)
                .setOffset(offsetof(FTerrainVertex, mPosition))
                .setElementStride(sizeof(FTerrainVertex)),
            nvrhi::VertexAttributeDesc()
                .setName("HEIGHT")
                .setFormat(nvrhi::Format::R32_FLOAT)
                .setBufferIndex(0)
                .setOffset(offsetof(FTerrainVertex, mHeight))
                .setElementStride(sizeof(FTerrainVertex))
        };
        mTerrainInputLayout = mDevice->createInputLayout(
            attributes,
            static_cast<uint32_t>(eastl::size(attributes)),
            mTerrainVertexShader);
        if (!mTerrainInputLayout)
        {
            mError = "NVRHI failed to create the terrain input layout.";
            return false;
        }

        nvrhi::RenderState terrainRenderState;
        terrainRenderState.depthStencilState
            .enableDepthTest()
            .enableDepthWrite()
            .setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual);
        terrainRenderState.rasterState.setCullNone();
        mTerrainPipeline = mDevice->createGraphicsPipeline(
            nvrhi::GraphicsPipelineDesc()
                .setInputLayout(mTerrainInputLayout)
                .setVertexShader(mTerrainVertexShader)
                .setPixelShader(mTerrainPixelShader)
                .addBindingLayout(mCameraBindingLayout)
                .addBindingLayout(mTerrainPixelBindingLayout)
                .setRenderState(terrainRenderState),
            nvrhi::FramebufferInfo()
                .addColorFormat(swapChainFormat)
                .setDepthFormat(nvrhi::Format::D32));

        nvrhi::RenderState overlayRenderState;
        overlayRenderState.depthStencilState
            .disableDepthTest()
            .disableDepthWrite();
        overlayRenderState.rasterState.setCullNone();
        overlayRenderState.blendState.targets[0]
            .enableBlend()
            .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
            .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha);
        mOverlayPipeline = mDevice->createGraphicsPipeline(
            nvrhi::GraphicsPipelineDesc()
                .setVertexShader(mOverlayVertexShader)
                .setPixelShader(mOverlayPixelShader)
                .setRenderState(overlayRenderState),
            nvrhi::FramebufferInfo().addColorFormat(swapChainFormat));

        if (!mTerrainPipeline || !mOverlayPipeline)
        {
            mError = "NVRHI failed to create terrain graphics pipelines.";
            return false;
        }
        return true;
    }

    bool FArdaTerrainRenderer::CreateCameraResources()
    {
        mCameraBuffer = mDevice->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(sizeof(FCameraSettings))
                .setIsConstantBuffer(true)
                .setDebugName("Terrain camera")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer));
        if (!mCameraBuffer)
        {
            mError = "NVRHI failed to create the terrain camera buffer.";
            return false;
        }

        mCameraBindingSet = mDevice->createBindingSet(
            nvrhi::BindingSetDesc().addItem(
                nvrhi::BindingSetItem::ConstantBuffer(0, mCameraBuffer)),
            mCameraBindingLayout);
        if (!mCameraBindingSet)
        {
            mError = "NVRHI failed to create the terrain camera binding set.";
            return false;
        }
        return true;
    }

    void FArdaTerrainRenderer::UpdateCamera(
        float forward,
        float right,
        float lookX,
        float lookY,
        float deltaSeconds)
    {
        constexpr float LookSensitivity = 0.0025f;
        constexpr float MoveSpeed = 1.1f;
        constexpr float PitchLimit = 1.50f;

        mElapsedSeconds += eastl::min(deltaSeconds, 0.1f);
        mCameraYaw += lookX * LookSensitivity;
        mCameraPitch = eastl::clamp(
            mCameraPitch - lookY * LookSensitivity,
            -PitchLimit,
            PitchLimit);

        const float cosPitch = std::cos(mCameraPitch);
        const float forwardVector[3] = {
            std::cos(mCameraYaw) * cosPitch,
            std::sin(mCameraYaw) * cosPitch,
            std::sin(mCameraPitch)};
        const float rightVector[3] = {
            -std::sin(mCameraYaw),
            std::cos(mCameraYaw),
            0.0f};

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
                (forwardVector[component] * forward +
                 rightVector[component] * right) *
                distance;
        }
    }

    bool FArdaTerrainRenderer::CreateSettingsUploadBuffer()
    {
        mSettingsUploadBuffer = mDevice->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(sizeof(FTerrainSettings))
                .setStructStride(sizeof(FTerrainSettings))
                .setDebugName("Terrain settings upload")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::CopySource));
        if (!mSettingsUploadBuffer)
        {
            mError = "NVRHI failed to create the terrain settings upload buffer.";
            return false;
        }
        return true;
    }

    bool FArdaTerrainRenderer::RenderFrame(backend::IArdaSwapChain& swapChain)
    {
        nvrhi::FramebufferHandle framebuffer;
        if (!swapChain.AcquireFrame(framebuffer))
        {
            mError = swapChain.GetError();
            return false;
        }

        const nvrhi::FramebufferDesc& framebufferDesc = framebuffer->getDesc();
        if (framebufferDesc.colorAttachments.empty() ||
            !framebufferDesc.colorAttachments[0].valid())
        {
            mError = "The acquired swap-chain framebuffer has no color attachment.";
            return false;
        }

        const nvrhi::FramebufferAttachment colorAttachment =
            framebufferDesc.colorAttachments[0];
        render_graph::FARDGBuilder graph(CreateGraphContext());

        render_graph::FARDGTextureRef backBuffer =
            graph.RegisterExternalTexture(
                colorAttachment.texture,
                nvrhi::ResourceStates::Present,
                "BackBuffer");
        render_graph::FARDGBufferRef settingsUpload =
            graph.RegisterExternalBuffer(
                mSettingsUploadBuffer,
                nvrhi::ResourceStates::CopySource,
                "TerrainSettingsUpload");
        render_graph::FARDGBufferRef cameraBuffer =
            graph.RegisterExternalBuffer(
                mCameraBuffer,
                nvrhi::ResourceStates::ConstantBuffer,
                "TerrainCamera");

        nvrhi::BufferDesc settingsDesc;
        settingsDesc
            .setDebugName("TerrainSettings")
            .setByteSize(sizeof(FTerrainSettings))
            .setStructStride(sizeof(FTerrainSettings));
        render_graph::FARDGBufferRef terrainSettings =
            graph.CreateBuffer(settingsDesc);

        nvrhi::TextureDesc heightmapDesc;
        heightmapDesc
            .setDebugName("Heightmap")
            .setWidth(HeightmapWidth)
            .setHeight(HeightmapHeight)
            .setMipLevels(2)
            .setFormat(nvrhi::Format::R32_FLOAT)
            .setIsUAV(true);
        render_graph::FARDGTextureRef heightmap =
            graph.CreateTexture(heightmapDesc);

        nvrhi::BufferDesc vertexDesc;
        vertexDesc
            .setDebugName("TerrainVertices")
            .setByteSize(TerrainVertexCount * sizeof(FTerrainVertex))
            .setStructStride(sizeof(FTerrainVertex))
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true);
        render_graph::FARDGBufferRef terrainVertices =
            graph.CreateBuffer(vertexDesc);

        nvrhi::BufferDesc indexDesc;
        indexDesc
            .setDebugName("TerrainIndices")
            .setByteSize(TerrainIndexCount * sizeof(uint32_t))
            .setStructStride(sizeof(uint32_t))
            .setCanHaveUAVs(true)
            .setIsIndexBuffer(true);
        render_graph::FARDGBufferRef terrainIndices =
            graph.CreateBuffer(indexDesc);

        nvrhi::TextureDesc depthDesc;
        depthDesc
            .setDebugName("TerrainDepth")
            .setWidth(swapChain.GetWidth())
            .setHeight(swapChain.GetHeight())
            .setFormat(nvrhi::Format::D32)
            .setIsRenderTarget(true);
        render_graph::FARDGTextureRef terrainDepth =
            graph.CreateTexture(depthDesc);

        render_graph::FARDGTextureViewDesc heightmapView;
        heightmapView.mTexture = heightmap->GetHandle();
        heightmapView.mSubresources =
            nvrhi::TextureSubresourceSet(0, 1, 0, 1);
        render_graph::FARDGTextureUAVRef heightmapUAV =
            graph.CreateTextureUAV("Heightmap mip 0 UAV", heightmapView);
        render_graph::FARDGTextureSRVRef heightmapSRV =
            graph.CreateTextureSRV("Heightmap mip 0 SRV", heightmapView);

        render_graph::FARDGBufferViewDesc vertexView;
        vertexView.mBuffer = terrainVertices->GetHandle();
        vertexView.mRange = nvrhi::EntireBuffer;
        render_graph::FARDGBufferUAVRef terrainVerticesUAV =
            graph.CreateBufferUAV("TerrainVertices UAV", vertexView);

        render_graph::FARDGBufferViewDesc indexView;
        indexView.mBuffer = terrainIndices->GetHandle();
        indexView.mRange = nvrhi::EntireBuffer;
        render_graph::FARDGBufferUAVRef terrainIndicesUAV =
            graph.CreateBufferUAV("TerrainIndices UAV", indexView);

        FTerrainSettings settingsData;
        settingsData.mTime = mElapsedSeconds;
        FInitializeTerrainSettingsParameters updateSettings;
        updateSettings.mDestination = {
            settingsUpload,
            nvrhi::ResourceStates::CopyDest,
            nvrhi::EntireBuffer};
        (void)graph.AddPass(
            "UpdateTerrainSettings",
            &updateSettings,
            render_graph::EARDGPassFlags::None,
            [settingsData](
                render_graph::FARDGPassExecutionContext& context,
                const FInitializeTerrainSettingsParameters& frozen)
            {
                context.mCommandList.writeBuffer(
                    context.GetBuffer(frozen.mDestination.mBuffer),
                    &settingsData,
                    sizeof(settingsData));
            });

        FUploadTerrainSettingsParameters upload;
        upload.mSource = {
            settingsUpload,
            nvrhi::ResourceStates::CopySource,
            nvrhi::EntireBuffer};
        upload.mDestination = {
            terrainSettings,
            nvrhi::ResourceStates::CopyDest,
            nvrhi::EntireBuffer};
        (void)graph.AddPass(
            "UploadTerrainSettings",
            &upload,
            render_graph::EARDGPassFlags::Copy,
            [](render_graph::FARDGPassExecutionContext& context,
               const FUploadTerrainSettingsParameters& frozen)
            {
                context.mCommandList.copyBuffer(
                    context.GetBuffer(frozen.mDestination.mBuffer),
                    0,
                    context.GetBuffer(frozen.mSource.mBuffer),
                    0,
                    sizeof(FTerrainSettings));
            });

        FGenerateNoiseHeightmapParameters generate;
        generate.mSettings = {
            terrainSettings,
            nvrhi::ResourceStates::NonPixelShaderResource,
            nvrhi::EntireBuffer};
        generate.mHeightmap = heightmapUAV;
        (void)graph.AddDispatchPass(
            "GenerateNoiseHeightmap",
            &generate,
            render_graph::FARDGDispatchArguments{
                DivideRoundUp(HeightmapWidth, 8),
                DivideRoundUp(HeightmapHeight, 8),
                1},
            [this](
                render_graph::FARDGPassExecutionContext& context,
                const FGenerateNoiseHeightmapParameters& frozen)
            {
                nvrhi::BindingSetHandle bindings = mDevice->createBindingSet(
                    nvrhi::BindingSetDesc()
                        .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(
                            0,
                            context.GetBuffer(frozen.mSettings.mBuffer)))
                        .addItem(nvrhi::BindingSetItem::Texture_UAV(
                            0,
                            context.GetTexture(frozen.mHeightmap),
                            nvrhi::Format::R32_FLOAT,
                            nvrhi::TextureSubresourceSet(0, 1, 0, 1))),
                    mGenerateBindingLayout);
                if (!bindings)
                {
                    ARDA_CHECK_MSG(
                        "Failed to create GenerateNoiseHeightmap bindings.");
                }
                context.mCommandList.setComputeState(
                    nvrhi::ComputeState()
                        .setPipeline(mGeneratePipeline)
                        .addBindingSet(bindings));
            },
            render_graph::EARDGPassFlags::AsyncCompute);

        FDebugHeightmapParameters debugHeightmap;
        debugHeightmap.mHeightmap = heightmapSRV;
        (void)graph.AddPass(
            "DebugHeightmap",
            &debugHeightmap,
            render_graph::EARDGPassFlags::None,
            [](const FDebugHeightmapParameters&)
            {
                // Intentionally has no output. Compilation culls this pass.
            });

        FErodeHeightmapParameters erode;
        erode.mHeightmap = heightmapUAV;
        (void)graph.AddDispatchPass(
            "ErodeHeightmap",
            &erode,
            render_graph::FARDGDispatchArguments{
                DivideRoundUp(HeightmapWidth, 8),
                DivideRoundUp(HeightmapHeight, 8),
                1},
            [this](
                render_graph::FARDGPassExecutionContext& context,
                const FErodeHeightmapParameters& frozen)
            {
                nvrhi::BindingSetHandle bindings = mDevice->createBindingSet(
                    nvrhi::BindingSetDesc().addItem(
                        nvrhi::BindingSetItem::Texture_UAV(
                            0,
                            context.GetTexture(frozen.mHeightmap),
                            nvrhi::Format::R32_FLOAT,
                            nvrhi::TextureSubresourceSet(0, 1, 0, 1))),
                    mErodeBindingLayout);
                if (!bindings)
                {
                    ARDA_CHECK_MSG("Failed to create ErodeHeightmap bindings.");
                }
                context.mCommandList.setComputeState(
                    nvrhi::ComputeState()
                        .setPipeline(mErodePipeline)
                        .addBindingSet(bindings));
            },
            render_graph::EARDGPassFlags::AsyncCompute);

        FTriangulateTerrainParameters triangulate;
        triangulate.mHeightmap = heightmapSRV;
        triangulate.mTerrainVertices = terrainVerticesUAV;
        triangulate.mTerrainIndices = terrainIndicesUAV;
        (void)graph.AddDispatchPass(
            "TriangulateTerrain",
            &triangulate,
            render_graph::FARDGDispatchArguments{
                DivideRoundUp(HeightmapWidth - 1, 8),
                DivideRoundUp(HeightmapHeight - 1, 8),
                1},
            [this](
                render_graph::FARDGPassExecutionContext& context,
                const FTriangulateTerrainParameters& frozen)
            {
                nvrhi::BindingSetHandle bindings = mDevice->createBindingSet(
                    nvrhi::BindingSetDesc()
                        .addItem(nvrhi::BindingSetItem::Texture_SRV(
                            0,
                            context.GetTexture(frozen.mHeightmap),
                            nvrhi::Format::R32_FLOAT,
                            nvrhi::TextureSubresourceSet(0, 1, 0, 1)))
                        .addItem(
                            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                                0,
                                context.GetBuffer(frozen.mTerrainVertices)))
                        .addItem(
                            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                                1,
                                context.GetBuffer(frozen.mTerrainIndices))),
                    mTriangulateBindingLayout);
                if (!bindings)
                {
                    ARDA_CHECK_MSG(
                        "Failed to create TriangulateTerrain bindings.");
                }
                context.mCommandList.setComputeState(
                    nvrhi::ComputeState()
                        .setPipeline(mTriangulatePipeline)
                        .addBindingSet(bindings));
            },
            render_graph::EARDGPassFlags::AsyncCompute);

        FRenderTerrainParameters render;
        render.mTerrainVertices = {
            terrainVertices,
            nvrhi::ResourceStates::VertexBuffer,
            nvrhi::EntireBuffer};
        render.mTerrainIndices = {
            terrainIndices,
            nvrhi::ResourceStates::IndexBuffer,
            nvrhi::EntireBuffer};
        render.mCamera = {
            cameraBuffer,
            nvrhi::ResourceStates::ConstantBuffer,
            nvrhi::EntireBuffer};
        render.mHeightmap = heightmapSRV;
        render.mTargets.mColor[0] = {
            backBuffer,
            colorAttachment.subresources};
        render.mTargets.mDepthStencil = {
            terrainDepth,
            nvrhi::AllSubresources};

        const uint32_t width = swapChain.GetWidth();
        const uint32_t height = swapChain.GetHeight();
        const float cosPitch = std::cos(mCameraPitch);
        const float forward[3] = {
            std::cos(mCameraYaw) * cosPitch,
            std::sin(mCameraYaw) * cosPitch,
            std::sin(mCameraPitch)};
        const float right[3] = {
            -std::sin(mCameraYaw),
            std::cos(mCameraYaw),
            0.0f};
        const float up[3] = {
            forward[1] * right[2] - forward[2] * right[1],
            forward[2] * right[0] - forward[0] * right[2],
            forward[0] * right[1] - forward[1] * right[0]};
        const float viewTranslation[3] = {
            -(mCameraPosition[0] * right[0] +
              mCameraPosition[1] * right[1] +
              mCameraPosition[2] * right[2]),
            -(mCameraPosition[0] * up[0] +
              mCameraPosition[1] * up[1] +
              mCameraPosition[2] * up[2]),
            -(mCameraPosition[0] * forward[0] +
              mCameraPosition[1] * forward[1] +
              mCameraPosition[2] * forward[2])};
        constexpr float NearPlane = 0.05f;
        constexpr float HorizontalHalfFov = 0.78539816f;
        const float xScale = 1.0f / std::tan(HorizontalHalfFov);
        const float yScale =
            xScale * static_cast<float>(width) / static_cast<float>(height);
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
        (void)graph.AddPass(
            "RenderTerrain",
            &render,
            render_graph::EARDGPassFlags::Raster,
            [this, width, height, cameraSettings](
                render_graph::FARDGPassExecutionContext& context,
                const FRenderTerrainParameters& frozen)
            {
                (void)context.GetTexture(
                    frozen.mTargets.mColor[0].mTexture);
                nvrhi::ITexture* depthTexture = context.GetTexture(
                    frozen.mTargets.mDepthStencil.mTexture);
                nvrhi::FramebufferHandle terrainFramebuffer =
                    mDevice->createFramebuffer(
                        nvrhi::FramebufferDesc()
                            .addColorAttachment(
                                context.GetTexture(
                                    frozen.mTargets.mColor[0].mTexture),
                                frozen.mTargets.mColor[0].mSubresources)
                            .setDepthAttachment(
                                depthTexture,
                                frozen.mTargets.mDepthStencil.mSubresources));
                if (!terrainFramebuffer)
                {
                    ARDA_CHECK_MSG(
                        "Failed to create the terrain depth framebuffer.");
                }
                nvrhi::utils::ClearColorAttachment(
                    &context.mCommandList,
                    terrainFramebuffer,
                    0,
                    nvrhi::Color(0.004f, 0.007f, 0.009f, 1.0f));
                context.mCommandList.clearDepthStencilTexture(
                    depthTexture,
                    frozen.mTargets.mDepthStencil.mSubresources,
                    true,
                    0.0f,
                    false,
                    0);
                context.mCommandList.writeBuffer(
                    context.GetBuffer(frozen.mCamera.mBuffer),
                    &cameraSettings,
                    sizeof(cameraSettings));
                nvrhi::BindingSetHandle terrainPixelBindings =
                    mDevice->createBindingSet(
                        nvrhi::BindingSetDesc().addItem(
                            nvrhi::BindingSetItem::Texture_SRV(
                                0,
                                context.GetTexture(frozen.mHeightmap),
                                nvrhi::Format::R32_FLOAT,
                                nvrhi::TextureSubresourceSet(0, 1, 0, 1))),
                        mTerrainPixelBindingLayout);
                if (!terrainPixelBindings)
                {
                    ARDA_CHECK_MSG(
                        "Failed to create terrain pixel bindings.");
                }

                const nvrhi::ViewportState viewport =
                    nvrhi::ViewportState().addViewportAndScissorRect(
                        nvrhi::Viewport(
                            static_cast<float>(width),
                            static_cast<float>(height)));
                nvrhi::GraphicsState state;
                state
                    .setPipeline(mTerrainPipeline)
                    .setFramebuffer(terrainFramebuffer)
                    .setViewport(viewport)
                    .addBindingSet(mCameraBindingSet)
                        .addBindingSet(terrainPixelBindings)
                    .addVertexBuffer(
                        nvrhi::VertexBufferBinding()
                            .setBuffer(context.GetBuffer(
                                frozen.mTerrainVertices.mBuffer))
                            .setSlot(0)
                            .setOffset(0))
                    .setIndexBuffer(
                        nvrhi::IndexBufferBinding()
                            .setBuffer(context.GetBuffer(
                                frozen.mTerrainIndices.mBuffer))
                            .setFormat(nvrhi::Format::R32_UINT)
                            .setOffset(0));
                context.mCommandList.setGraphicsState(state);
                context.mCommandList.drawIndexed(
                    nvrhi::DrawArguments().setVertexCount(TerrainIndexCount));
            });

        FTerrainOverlayParameters overlay;
        overlay.mTargets.mColor[0] = {
            backBuffer,
            colorAttachment.subresources};
        (void)graph.AddPass(
            "TerrainOverlay",
            &overlay,
            render_graph::EARDGPassFlags::Raster,
            [this, framebuffer, width, height](
                render_graph::FARDGPassExecutionContext& context,
                const FTerrainOverlayParameters& frozen)
            {
                (void)context.GetTexture(
                    frozen.mTargets.mColor[0].mTexture);
                const nvrhi::ViewportState viewport =
                    nvrhi::ViewportState().addViewportAndScissorRect(
                        nvrhi::Viewport(
                            static_cast<float>(width),
                            static_cast<float>(height)));
                context.mCommandList.setGraphicsState(
                    nvrhi::GraphicsState()
                        .setPipeline(mOverlayPipeline)
                        .setFramebuffer(framebuffer)
                        .setViewport(viewport));
                context.mCommandList.draw(
                    nvrhi::DrawArguments().setVertexCount(3));
            });

        swapChain.PrepareSubmit();
        (void)graph.Execute();

        if (!swapChain.Present())
        {
            mError = swapChain.GetError();
            return false;
        }

        mError.clear();
        return true;
    }

    render_graph::FARDGRenderGraphContext
    FArdaTerrainRenderer::CreateGraphContext() const
    {
        render_graph::FARDGRenderGraphContext context;
        context.mDevice = mDevice;
        context.mQueueCapabilities = mQueueCapabilities;
        return context;
    }

    bool FArdaTerrainRenderer::LoadBinary(
        const std::filesystem::path& path,
        eastl::vector<uint8_t>& binary,
        eastl::string& error)
    {
        const std::string pathString = path.string();
        const eastl::string displayPath(pathString.data(), pathString.size());
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            error = "Unable to open shader: " + displayPath;
            return false;
        }

        const auto size = stream.tellg();
        if (size <= 0)
        {
            error = "Shader is empty: " + displayPath;
            return false;
        }

        binary.resize(static_cast<size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(binary.data()), size);
        if (!stream)
        {
            error = "Unable to read shader: " + displayPath;
            binary.clear();
            return false;
        }
        return true;
    }
}
