#include "ArdaNVRHITestPch.h"

#include "ArdaTriangleRenderer.h"

namespace arda::tests::nvrhi_test
{
    namespace
    {
        struct FArdaVertex
        {
            float mPosition[2];
            float mColor[3];
        };

        constexpr FArdaVertex Vertices[] = {
            { {  0.0f,  0.6f }, { 1.0f, 0.1f, 0.1f } },
            { {  0.6f, -0.6f }, { 0.1f, 1.0f, 0.1f } },
            { { -0.6f, -0.6f }, { 0.1f, 0.2f, 1.0f } }
        };

        constexpr uint16_t Indices[] = { 0, 1, 2 };

        ARDG_BEGIN_PARAMETER_STRUCT(FArdaTriangleUploadParameters)
            ARDG_BUFFER_ACCESS(mVertexBuffer)
            ARDG_BUFFER_ACCESS(mIndexBuffer)
        ARDG_END_PARAMETER_STRUCT()

        ARDG_BEGIN_PARAMETER_STRUCT(FArdaTriangleRasterParameters)
            ARDG_BUFFER_ACCESS(mVertexBuffer)
            ARDG_BUFFER_ACCESS(mIndexBuffer)
            ARDG_RENDER_TARGET_BINDING_SLOTS(mRenderTargets)
        ARDG_END_PARAMETER_STRUCT()
    }

    bool FArdaTriangleRenderer::Initialize(
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

        const bool vulkan =
            deviceContext.mBackend == backend::EArdaBackendType::Vulkan;
        std::vector<uint8_t> vertexBinary;
        std::vector<uint8_t> pixelBinary;
        if (!LoadBinary(
                shaderDirectory / (vulkan ? "TriangleVS.spv" : "TriangleVS.dxil"),
                vertexBinary,
                mError) ||
            !LoadBinary(
                shaderDirectory / (vulkan ? "TrianglePS.spv" : "TrianglePS.dxil"),
                pixelBinary,
                mError))
        {
            return false;
        }

        mVertexShader = mDevice->createShader(
                nvrhi::ShaderDesc()
                    .setShaderType(nvrhi::ShaderType::Vertex)
                    .setEntryName("VSMain")
                    .setDebugName("Triangle vertex shader"),
                vertexBinary.data(),
                vertexBinary.size());

            mPixelShader = mDevice->createShader(
                nvrhi::ShaderDesc()
                    .setShaderType(nvrhi::ShaderType::Pixel)
                    .setEntryName("PSMain")
                    .setDebugName("Triangle pixel shader"),
                pixelBinary.data(),
                pixelBinary.size());

            if (!mVertexShader || !mPixelShader)
            {
                mError = "NVRHI failed to create the triangle shaders.";
                return false;
            }

            const nvrhi::VertexAttributeDesc attributes[] = {
                nvrhi::VertexAttributeDesc()
                    .setName("POSITION")
                    .setFormat(nvrhi::Format::RG32_FLOAT)
                    .setBufferIndex(0)
                    .setOffset(offsetof(FArdaVertex, mPosition))
                    .setElementStride(sizeof(FArdaVertex)),
                nvrhi::VertexAttributeDesc()
                    .setName("COLOR")
                    .setFormat(nvrhi::Format::RGB32_FLOAT)
                    .setBufferIndex(0)
                    .setOffset(offsetof(FArdaVertex, mColor))
                    .setElementStride(sizeof(FArdaVertex))
            };

            mInputLayout = mDevice->createInputLayout(
                attributes,
                static_cast<uint32_t>(std::size(attributes)),
                mVertexShader);
            if (!mInputLayout)
            {
                mError = "NVRHI failed to create the triangle input layout.";
                return false;
            }

            nvrhi::RenderState renderState;
            renderState.depthStencilState.disableDepthTest().disableDepthWrite();
            renderState.rasterState.setCullNone();

            const auto pipelineDesc = nvrhi::GraphicsPipelineDesc()
                .setInputLayout(mInputLayout)
                .setVertexShader(mVertexShader)
                .setPixelShader(mPixelShader)
                .setRenderState(renderState);
            const auto framebufferInfo = nvrhi::FramebufferInfo().addColorFormat(swapChainFormat);

            mPipeline = mDevice->createGraphicsPipeline(pipelineDesc, framebufferInfo);
            if (!mPipeline)
            {
                mError = "NVRHI failed to create the triangle graphics pipeline.";
                return false;
            }

            mVertexBuffer = mDevice->createBuffer(
                nvrhi::BufferDesc()
                    .setByteSize(sizeof(Vertices))
                    .setIsVertexBuffer(true)
                    .setDebugName("Triangle vertex buffer")
                    .enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer));
            mIndexBuffer = mDevice->createBuffer(
                nvrhi::BufferDesc()
                    .setByteSize(sizeof(Indices))
                    .setIsIndexBuffer(true)
                    .setDebugName("Triangle index buffer")
                    .enableAutomaticStateTracking(nvrhi::ResourceStates::IndexBuffer));

            if (!mVertexBuffer || !mIndexBuffer)
            {
                mError = "NVRHI failed to create the triangle geometry buffers.";
                return false;
            }

            render_graph::FARDGBuilder graph(CreateGraphContext());
            render_graph::FARDGBufferRef vertexBuffer =
                graph.RegisterExternalBuffer(
                    mVertexBuffer,
                    nvrhi::ResourceStates::VertexBuffer,
                    "Triangle vertex buffer");
            render_graph::FARDGBufferRef indexBuffer =
                graph.RegisterExternalBuffer(
                    mIndexBuffer,
                    nvrhi::ResourceStates::IndexBuffer,
                    "Triangle index buffer");

            FArdaTriangleUploadParameters parameters;
            parameters.mVertexBuffer = {
                vertexBuffer,
                nvrhi::ResourceStates::CopyDest,
                nvrhi::EntireBuffer};
            parameters.mIndexBuffer = {
                indexBuffer,
                nvrhi::ResourceStates::CopyDest,
                nvrhi::EntireBuffer};
            (void)graph.AddPass(
                "Upload triangle geometry",
                &parameters,
                render_graph::EARDGPassFlags::None,
                [](render_graph::FARDGPassExecutionContext& context,
                   const FArdaTriangleUploadParameters& frozen)
                {
                    context.mCommandList.writeBuffer(
                        context.GetBuffer(frozen.mVertexBuffer.mBuffer),
                        Vertices,
                        sizeof(Vertices));
                    context.mCommandList.writeBuffer(
                        context.GetBuffer(frozen.mIndexBuffer.mBuffer),
                        Indices,
                        sizeof(Indices));
                });
            (void)graph.Execute();
        if (!mDevice->waitForIdle())
        {
            mError = "The GPU failed while uploading triangle geometry.";
            return false;
        }

        mError.clear();
        return true;
    }

    bool FArdaTriangleRenderer::RenderFrame(backend::IArdaSwapChain& swapChain)
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
        nvrhi::TextureHandle renderTarget = colorAttachment.texture;

        render_graph::FARDGBuilder graph(CreateGraphContext());
            render_graph::FARDGTextureRef graphRenderTarget =
                graph.RegisterExternalTexture(
                    renderTarget,
                    nvrhi::ResourceStates::Present,
                    "Swap-chain color");
            render_graph::FARDGBufferRef graphVertexBuffer =
                graph.RegisterExternalBuffer(
                    mVertexBuffer,
                    nvrhi::ResourceStates::VertexBuffer,
                    "Triangle vertex buffer");
            render_graph::FARDGBufferRef graphIndexBuffer =
                graph.RegisterExternalBuffer(
                    mIndexBuffer,
                    nvrhi::ResourceStates::IndexBuffer,
                    "Triangle index buffer");

            FArdaTriangleRasterParameters parameters;
            parameters.mVertexBuffer = {
                graphVertexBuffer,
                nvrhi::ResourceStates::VertexBuffer,
                nvrhi::EntireBuffer};
            parameters.mIndexBuffer = {
                graphIndexBuffer,
                nvrhi::ResourceStates::IndexBuffer,
                nvrhi::EntireBuffer};
            parameters.mRenderTargets.mColor[0] = {
                graphRenderTarget,
                colorAttachment.subresources};

            const uint32_t width = swapChain.GetWidth();
            const uint32_t height = swapChain.GetHeight();
            (void)graph.AddPass(
                "Render triangle",
                &parameters,
                render_graph::EARDGPassFlags::Raster,
                [this, framebuffer, width, height](
                    render_graph::FARDGPassExecutionContext& context,
                    const FArdaTriangleRasterParameters& frozen)
                {
                    (void)context.GetTexture(
                        frozen.mRenderTargets.mColor[0].mTexture);
                    nvrhi::utils::ClearColorAttachment(
                        &context.mCommandList,
                        framebuffer,
                        0,
                        nvrhi::Color(0.025f, 0.035f, 0.06f, 1.f));

                    const auto viewport =
                        nvrhi::ViewportState().addViewportAndScissorRect(
                            nvrhi::Viewport(
                                static_cast<float>(width),
                                static_cast<float>(height)));
                    const auto graphicsState = nvrhi::GraphicsState()
                        .setPipeline(mPipeline)
                        .setFramebuffer(framebuffer)
                        .setViewport(viewport)
                        .addVertexBuffer(
                            nvrhi::VertexBufferBinding()
                                .setBuffer(context.GetBuffer(
                                    frozen.mVertexBuffer.mBuffer))
                                .setSlot(0)
                                .setOffset(0))
                        .setIndexBuffer(
                            nvrhi::IndexBufferBinding()
                                .setBuffer(context.GetBuffer(
                                    frozen.mIndexBuffer.mBuffer))
                                .setFormat(nvrhi::Format::R16_UINT)
                                .setOffset(0));

                    context.mCommandList.setGraphicsState(graphicsState);
                    context.mCommandList.drawIndexed(
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
    FArdaTriangleRenderer::CreateGraphContext() const
    {
        render_graph::FARDGRenderGraphContext context;
        context.mDevice = mDevice;
        context.mQueueCapabilities = mQueueCapabilities;
        return context;
    }

    bool FArdaTriangleRenderer::LoadBinary(
        const std::filesystem::path& path,
        std::vector<uint8_t>& binary,
        std::string& error)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            error = "Unable to open shader: " + path.string();
            return false;
        }

        const auto size = stream.tellg();
        if (size <= 0)
        {
            error = "Shader is empty: " + path.string();
            return false;
        }

        binary.resize(static_cast<size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(binary.data()), size);
        if (!stream)
        {
            error = "Unable to read shader: " + path.string();
            binary.clear();
            return false;
        }
        return true;
    }
}
