#include "NVRHITestPch.h"

#include "TriangleRenderer.h"

namespace arda::tests::nvrhi_test
{
    namespace
    {
        struct Vertex
        {
            float position[2];
            float color[3];
        };

        constexpr Vertex Vertices[] = {
            { {  0.0f,  0.6f }, { 1.0f, 0.1f, 0.1f } },
            { {  0.6f, -0.6f }, { 0.1f, 1.0f, 0.1f } },
            { { -0.6f, -0.6f }, { 0.1f, 0.2f, 1.0f } }
        };

        constexpr uint16_t Indices[] = { 0, 1, 2 };
    }

    bool TriangleRenderer::Initialize(
        nvrhi::DeviceHandle device,
        nvrhi::Format swapChainFormat,
        BackendKind backendKind,
        const std::filesystem::path& shaderDirectory)
    {
        m_device = std::move(device);

        try
        {
            const bool vulkan = backendKind == BackendKind::Vulkan;
            const auto vertexBinary = LoadBinary(shaderDirectory / (vulkan ? "TriangleVS.spv" : "TriangleVS.dxil"));
            const auto pixelBinary = LoadBinary(shaderDirectory / (vulkan ? "TrianglePS.spv" : "TrianglePS.dxil"));

            m_vertexShader = m_device->createShader(
                nvrhi::ShaderDesc()
                    .setShaderType(nvrhi::ShaderType::Vertex)
                    .setEntryName("VSMain")
                    .setDebugName("Triangle vertex shader"),
                vertexBinary.data(),
                vertexBinary.size());

            m_pixelShader = m_device->createShader(
                nvrhi::ShaderDesc()
                    .setShaderType(nvrhi::ShaderType::Pixel)
                    .setEntryName("PSMain")
                    .setDebugName("Triangle pixel shader"),
                pixelBinary.data(),
                pixelBinary.size());

            if (!m_vertexShader || !m_pixelShader)
            {
                m_error = "NVRHI failed to create the triangle shaders.";
                return false;
            }

            const nvrhi::VertexAttributeDesc attributes[] = {
                nvrhi::VertexAttributeDesc()
                    .setName("POSITION")
                    .setFormat(nvrhi::Format::RG32_FLOAT)
                    .setBufferIndex(0)
                    .setOffset(offsetof(Vertex, position))
                    .setElementStride(sizeof(Vertex)),
                nvrhi::VertexAttributeDesc()
                    .setName("COLOR")
                    .setFormat(nvrhi::Format::RGB32_FLOAT)
                    .setBufferIndex(0)
                    .setOffset(offsetof(Vertex, color))
                    .setElementStride(sizeof(Vertex))
            };

            m_inputLayout = m_device->createInputLayout(
                attributes,
                static_cast<uint32_t>(std::size(attributes)),
                m_vertexShader);
            if (!m_inputLayout)
            {
                m_error = "NVRHI failed to create the triangle input layout.";
                return false;
            }

            nvrhi::RenderState renderState;
            renderState.depthStencilState.disableDepthTest().disableDepthWrite();
            renderState.rasterState.setCullNone();

            const auto pipelineDesc = nvrhi::GraphicsPipelineDesc()
                .setInputLayout(m_inputLayout)
                .setVertexShader(m_vertexShader)
                .setPixelShader(m_pixelShader)
                .setRenderState(renderState);
            const auto framebufferInfo = nvrhi::FramebufferInfo().addColorFormat(swapChainFormat);

            m_pipeline = m_device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
            if (!m_pipeline)
            {
                m_error = "NVRHI failed to create the triangle graphics pipeline.";
                return false;
            }

            m_vertexBuffer = m_device->createBuffer(
                nvrhi::BufferDesc()
                    .setByteSize(sizeof(Vertices))
                    .setIsVertexBuffer(true)
                    .setDebugName("Triangle vertex buffer")
                    .enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer));
            m_indexBuffer = m_device->createBuffer(
                nvrhi::BufferDesc()
                    .setByteSize(sizeof(Indices))
                    .setIsIndexBuffer(true)
                    .setDebugName("Triangle index buffer")
                    .enableAutomaticStateTracking(nvrhi::ResourceStates::IndexBuffer));

            if (!m_vertexBuffer || !m_indexBuffer)
            {
                m_error = "NVRHI failed to create the triangle geometry buffers.";
                return false;
            }

            m_commandList = m_device->createCommandList();
            if (!m_commandList)
            {
                m_error = "NVRHI failed to create a graphics command list.";
                return false;
            }

            m_commandList->open();
            m_commandList->writeBuffer(m_vertexBuffer, Vertices, sizeof(Vertices));
            m_commandList->writeBuffer(m_indexBuffer, Indices, sizeof(Indices));
            m_commandList->close();
            m_device->executeCommandList(m_commandList);
            if (!m_device->waitForIdle())
            {
                m_error = "The GPU failed while uploading triangle geometry.";
                return false;
            }
        }
        catch (const std::exception& error)
        {
            m_error = error.what();
            return false;
        }

        return true;
    }

    bool TriangleRenderer::RenderFrame(Backend& backend)
    {
        nvrhi::FramebufferHandle framebuffer;
        if (!backend.AcquireFrame(framebuffer))
        {
            m_error = backend.GetError();
            return false;
        }

        m_commandList->open();
        nvrhi::utils::ClearColorAttachment(
            m_commandList,
            framebuffer,
            0,
            nvrhi::Color(0.025f, 0.035f, 0.06f, 1.f));

        const auto viewport = nvrhi::ViewportState().addViewportAndScissorRect(
            nvrhi::Viewport(
                static_cast<float>(backend.GetWidth()),
                static_cast<float>(backend.GetHeight())));
        const auto graphicsState = nvrhi::GraphicsState()
            .setPipeline(m_pipeline)
            .setFramebuffer(framebuffer)
            .setViewport(viewport)
            .addVertexBuffer(
                nvrhi::VertexBufferBinding()
                    .setBuffer(m_vertexBuffer)
                    .setSlot(0)
                    .setOffset(0))
            .setIndexBuffer(
                nvrhi::IndexBufferBinding()
                    .setBuffer(m_indexBuffer)
                    .setFormat(nvrhi::Format::R16_UINT)
                    .setOffset(0));

        m_commandList->setGraphicsState(graphicsState);
        m_commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(3));
        m_commandList->close();

        backend.PrepareSubmit();
        m_device->executeCommandList(m_commandList);
        m_device->runGarbageCollection();

        if (!backend.Present())
        {
            m_error = backend.GetError();
            return false;
        }

        return true;
    }

    std::vector<uint8_t> TriangleRenderer::LoadBinary(const std::filesystem::path& path)
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
