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
    }

    bool FArdaTriangleRenderer::Initialize(
        nvrhi::DeviceHandle device,
        nvrhi::Format swapChainFormat,
        backend::EArdaBackendType backendType,
        const std::filesystem::path& shaderDirectory)
    {
        mDevice = std::move(device);

        try
        {
            const bool vulkan = backendType == backend::EArdaBackendType::Vulkan;
            const auto vertexBinary = LoadBinary(shaderDirectory / (vulkan ? "TriangleVS.spv" : "TriangleVS.dxil"));
            const auto pixelBinary = LoadBinary(shaderDirectory / (vulkan ? "TrianglePS.spv" : "TrianglePS.dxil"));

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

            mCommandList = mDevice->createCommandList();
            if (!mCommandList)
            {
                mError = "NVRHI failed to create a graphics command list.";
                return false;
            }

            mCommandList->open();
            mCommandList->writeBuffer(mVertexBuffer, Vertices, sizeof(Vertices));
            mCommandList->writeBuffer(mIndexBuffer, Indices, sizeof(Indices));
            mCommandList->close();
            mDevice->executeCommandList(mCommandList);
            if (!mDevice->waitForIdle())
            {
                mError = "The GPU failed while uploading triangle geometry.";
                return false;
            }
        }
        catch (const std::exception& error)
        {
            mError = error.what();
            return false;
        }

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

        mCommandList->open();
        nvrhi::utils::ClearColorAttachment(
            mCommandList,
            framebuffer,
            0,
            nvrhi::Color(0.025f, 0.035f, 0.06f, 1.f));

        const auto viewport = nvrhi::ViewportState().addViewportAndScissorRect(
            nvrhi::Viewport(
                static_cast<float>(swapChain.GetWidth()),
                static_cast<float>(swapChain.GetHeight())));
        const auto graphicsState = nvrhi::GraphicsState()
            .setPipeline(mPipeline)
            .setFramebuffer(framebuffer)
            .setViewport(viewport)
            .addVertexBuffer(
                nvrhi::VertexBufferBinding()
                    .setBuffer(mVertexBuffer)
                    .setSlot(0)
                    .setOffset(0))
            .setIndexBuffer(
                nvrhi::IndexBufferBinding()
                    .setBuffer(mIndexBuffer)
                    .setFormat(nvrhi::Format::R16_UINT)
                    .setOffset(0));

        mCommandList->setGraphicsState(graphicsState);
        mCommandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(3));
        mCommandList->close();

        swapChain.PrepareSubmit();
        mDevice->executeCommandList(mCommandList);
        mDevice->runGarbageCollection();

        if (!swapChain.Present())
        {
            mError = swapChain.GetError();
            return false;
        }

        return true;
    }

    std::vector<uint8_t> FArdaTriangleRenderer::LoadBinary(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            throw std::runtime_error("Unable to open shader: " + path.string());
        }

        const auto size = stream.tellg();
        if (size <= 0)
        {
            throw std::runtime_error("Shader is empty: " + path.string());
        }

        std::vector<uint8_t> binary(static_cast<size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(binary.data()), size);
        if (!stream)
        {
            throw std::runtime_error("Unable to read shader: " + path.string());
        }
        return binary;
    }
}
