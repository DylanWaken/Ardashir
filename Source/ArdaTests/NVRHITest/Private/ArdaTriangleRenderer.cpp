#include "ArdaRHITestPch.h"

#include "ArdaTriangleRenderer.h"

namespace arda::tests::rhi_test
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
        rhi::EArdaRHIFormat swapChainFormat,
        const std::filesystem::path& shaderDirectory)
    {
        mDevice = deviceContext.mDevice;
        mQueueCapabilities = {
            deviceContext.mQueueCapabilities.mbGraphics,
            deviceContext.mQueueCapabilities.mbCompute,
            deviceContext.mQueueCapabilities.mbCopy};
        if (!mDevice || !mQueueCapabilities.mbGraphics)
        {
            mError = "The initialized backend does not expose a graphics device.";
            return false;
        }

        const bool vulkan = deviceContext.mBackend == backend::EArdaBackendType::Vulkan;
        eastl::vector<uint8_t> vertexBinary;
        eastl::vector<uint8_t> pixelBinary;
        if (!LoadBinary(shaderDirectory / (vulkan ? "TriangleVS.spv" : "TriangleVS.dxil"), vertexBinary, mError) ||
            !LoadBinary(shaderDirectory / (vulkan ? "TrianglePS.spv" : "TrianglePS.dxil"), pixelBinary, mError))
        {
            return false;
        }

        rhi::FArdaRHIShaderDesc shaderDesc;
        shaderDesc.mStage = rhi::EArdaRHIShaderStage::Vertex;
        shaderDesc.mBytecode = vertexBinary.data();
        shaderDesc.mBytecodeSize = vertexBinary.size();
        shaderDesc.mEntryPoint = "VSMain";
        shaderDesc.mDebugName = "Triangle vertex shader";
        auto vertexShader = mDevice->CreateShader(shaderDesc);
        shaderDesc.mStage = rhi::EArdaRHIShaderStage::Pixel;
        shaderDesc.mBytecode = pixelBinary.data();
        shaderDesc.mBytecodeSize = pixelBinary.size();
        shaderDesc.mEntryPoint = "PSMain";
        shaderDesc.mDebugName = "Triangle pixel shader";
        auto pixelShader = mDevice->CreateShader(shaderDesc);
        if (!vertexShader || !pixelShader)
        {
            mError = "RHI failed to create the triangle shaders.";
            return false;
        }
        mVertexShader = eastl::move(vertexShader.mValue);
        mPixelShader = eastl::move(pixelShader.mValue);

        eastl::vector<rhi::FArdaRHIVertexAttributeDesc> attributes(2);
        attributes[0].mSemanticName = "POSITION";
        attributes[0].mFormat = rhi::EArdaRHIFormat::RG32Float;
        attributes[0].mOffset = offsetof(FArdaVertex, mPosition);
        attributes[0].mElementStride = sizeof(FArdaVertex);
        attributes[1].mSemanticName = "COLOR";
        attributes[1].mFormat = rhi::EArdaRHIFormat::RGB32Float;
        attributes[1].mOffset = offsetof(FArdaVertex, mColor);
        attributes[1].mElementStride = sizeof(FArdaVertex);
        auto inputLayout = mDevice->CreateInputLayout(attributes, mVertexShader);
        if (!inputLayout)
        {
            mError = inputLayout.mStatus.mMessage;
            return false;
        }
        mInputLayout = eastl::move(inputLayout.mValue);

        rhi::FArdaRHIGraphicsPipelineDesc pipelineDesc;
        pipelineDesc.mInputLayout = mInputLayout;
        pipelineDesc.mVertexShader = mVertexShader;
        pipelineDesc.mPixelShader = mPixelShader;
        pipelineDesc.mDepthStencilState.mbDepthTest = false;
        pipelineDesc.mDepthStencilState.mbDepthWrite = false;
        pipelineDesc.mRasterState.mCullMode = rhi::EArdaRHICullMode::None;
        pipelineDesc.mColorFormats.push_back(swapChainFormat);
        pipelineDesc.mDebugName = "Triangle pipeline";
        auto pipeline = mDevice->CreateGraphicsPipeline(pipelineDesc);
        if (!pipeline)
        {
            mError = pipeline.mStatus.mMessage;
            return false;
        }
        mPipeline = eastl::move(pipeline.mValue);

        rhi::FArdaRHIBufferDesc bufferDesc;
        bufferDesc.mByteSize = sizeof(Vertices);
        bufferDesc.mUsage = rhi::EArdaRHIBufferUsage::Vertex;
        bufferDesc.mInitialState = rhi::EArdaRHIResourceState::VertexBuffer;
        bufferDesc.mbKeepInitialState = true;
        bufferDesc.mDebugName = "Triangle vertex buffer";
        auto vertexBuffer = mDevice->CreateBuffer(bufferDesc);
        bufferDesc.mByteSize = sizeof(Indices);
        bufferDesc.mUsage = rhi::EArdaRHIBufferUsage::Index;
        bufferDesc.mInitialState = rhi::EArdaRHIResourceState::IndexBuffer;
        bufferDesc.mDebugName = "Triangle index buffer";
        auto indexBuffer = mDevice->CreateBuffer(bufferDesc);
        if (!vertexBuffer || !indexBuffer)
        {
            mError = "RHI failed to create the triangle geometry buffers.";
            return false;
        }
        mVertexBuffer = eastl::move(vertexBuffer.mValue);
        mIndexBuffer = eastl::move(indexBuffer.mValue);

        render_graph::FARDGBuilder graph(CreateGraphContext());
        auto* graphVertexBuffer = graph.RegisterExternalBuffer(
            mVertexBuffer, rhi::EArdaRHIResourceState::VertexBuffer, "Triangle vertex buffer");
        auto* graphIndexBuffer = graph.RegisterExternalBuffer(
            mIndexBuffer, rhi::EArdaRHIResourceState::IndexBuffer, "Triangle index buffer");
        FArdaTriangleUploadParameters parameters;
        parameters.mVertexBuffer = {
            graphVertexBuffer, rhi::EArdaRHIResourceState::CopyDest, {}};
        parameters.mIndexBuffer = {
            graphIndexBuffer, rhi::EArdaRHIResourceState::CopyDest, {}};
        (void)graph.AddPass(
            "Upload triangle geometry",
            &parameters,
            render_graph::EARDGPassFlags::None,
            [](render_graph::FARDGPassExecutionContext& context,
               const FArdaTriangleUploadParameters& frozen)
            {
                (void)context.mCommandList.WriteBuffer(
                    *context.GetBuffer(frozen.mVertexBuffer.mBuffer), Vertices, sizeof(Vertices));
                (void)context.mCommandList.WriteBuffer(
                    *context.GetBuffer(frozen.mIndexBuffer.mBuffer), Indices, sizeof(Indices));
            });
        (void)graph.Execute();
        const auto idle = mDevice->WaitForIdle();
        if (!idle)
        {
            mError = idle.mMessage;
            return false;
        }
        mError.clear();
        return true;
    }

    bool FArdaTriangleRenderer::RenderFrame(backend::IArdaSwapChain& swapChain)
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

        render_graph::FARDGBuilder graph(CreateGraphContext());
        auto* graphRenderTarget = graph.RegisterExternalTexture(
            colorAttachment.mTexture, rhi::EArdaRHIResourceState::Present, "Swap-chain color");
        auto* graphVertexBuffer = graph.RegisterExternalBuffer(
            mVertexBuffer, rhi::EArdaRHIResourceState::VertexBuffer, "Triangle vertex buffer");
        auto* graphIndexBuffer = graph.RegisterExternalBuffer(
            mIndexBuffer, rhi::EArdaRHIResourceState::IndexBuffer, "Triangle index buffer");

        FArdaTriangleRasterParameters parameters;
        parameters.mVertexBuffer = {
            graphVertexBuffer, rhi::EArdaRHIResourceState::VertexBuffer, {}};
        parameters.mIndexBuffer = {
            graphIndexBuffer, rhi::EArdaRHIResourceState::IndexBuffer, {}};
        parameters.mRenderTargets.mColor[0] = {
            graphRenderTarget, colorAttachment.mAttachment.mSubresources};
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
                auto* target = context.GetTexture(frozen.mRenderTargets.mColor[0].mTexture);
                (void)context.mCommandList.ClearTexture(
                    *target, frozen.mRenderTargets.mColor[0].mSubresources,
                    { 0.025f, 0.035f, 0.06f, 1.f });
                rhi::FArdaRHIGraphicsState state;
                state.mPipeline = mPipeline;
                state.mFramebuffer = framebuffer;
                state.mVertexBuffers.push_back({
                    rhi::FArdaRHIBufferRef(context.GetBuffer(frozen.mVertexBuffer.mBuffer)), 0, 0});
                state.mIndexBuffer.Reset(context.GetBuffer(frozen.mIndexBuffer.mBuffer));
                state.mIndexFormat = rhi::EArdaRHIFormat::R16UInt;
                state.mViewports.push_back({
                    0.f, static_cast<float>(width), 0.f, static_cast<float>(height), 0.f, 1.f});
                state.mScissors.push_back({
                    0, static_cast<int32_t>(width), 0, static_cast<int32_t>(height)});
                (void)context.mCommandList.SetGraphicsState(state);
                context.mCommandList.DrawIndexed({ 3 });
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

    render_graph::FARDGRenderGraphContext FArdaTriangleRenderer::CreateGraphContext() const
    {
        render_graph::FARDGRenderGraphContext context;
        context.mDevice = mDevice;
        context.mQueueCapabilities = mQueueCapabilities;
        return context;
    }

    bool FArdaTriangleRenderer::LoadBinary(
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
