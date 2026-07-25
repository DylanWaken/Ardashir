#include "NVRHITestPch.h"

#include "D3D12Backend.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace arda::tests::nvrhi_test
{
    D3D12Backend::~D3D12Backend()
    {
        WaitForIdle();
        ReleaseSwapChainResources();
        m_device = nullptr;
        m_nativeDevice = nullptr;
    }

    InitializeResult D3D12Backend::Initialize(
        GLFWwindow* window,
        uint32_t width,
        uint32_t height,
        nvrhi::IMessageCallback* messageCallback)
    {
        m_window = glfwGetWin32Window(window);
        m_width = width;
        m_height = height;
        if (!m_window)
        {
            m_error = "GLFW did not provide a Win32 window handle.";
            return InitializeResult::Failure;
        }

        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory))))
        {
            m_error = "CreateDXGIFactory2 failed; D3D12 is unavailable.";
            return InitializeResult::Unavailable;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for (UINT index = 0;
             m_factory->EnumAdapterByGpuPreference(
                 index,
                 DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                 IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++index)
        {
            DXGI_ADAPTER_DESC1 description{};
            adapter->GetDesc1(&description);
            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
            adapter.Reset();
        }

        if (!adapter)
        {
            if (FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))))
            {
                m_error = "No D3D12 adapter or WARP adapter is available.";
                return InitializeResult::Unavailable;
            }
        }

        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_d3dDevice))))
        {
            m_error = "D3D12CreateDevice failed.";
            return InitializeResult::Unavailable;
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue))))
        {
            m_error = "ID3D12Device::CreateCommandQueue failed.";
            return InitializeResult::Failure;
        }

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
        swapChainDesc.Width = width;
        swapChainDesc.Height = height;
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = BufferCount;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
        if (FAILED(m_factory->CreateSwapChainForHwnd(
            m_queue.Get(),
            m_window,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain)))
        {
            m_error = "IDXGIFactory::CreateSwapChainForHwnd failed.";
            return InitializeResult::Failure;
        }

        m_factory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(swapChain.As(&m_swapChain)))
        {
            m_error = "The D3D12 swap chain does not expose IDXGISwapChain3.";
            return InitializeResult::Failure;
        }

        nvrhi::d3d12::DeviceDesc deviceDesc;
        deviceDesc.errorCB = messageCallback;
        deviceDesc.pDevice = m_d3dDevice.Get();
        deviceDesc.pGraphicsCommandQueue = m_queue.Get();
        m_nativeDevice = nvrhi::d3d12::createDevice(deviceDesc);
        if (!m_nativeDevice)
        {
            m_error = "nvrhi::d3d12::createDevice failed.";
            return InitializeResult::Failure;
        }

        m_device = nvrhi::validation::createValidationLayer(m_nativeDevice);
        if (!m_device || !CreateSwapChainResources())
        {
            if (m_error.empty())
            {
                m_error = "Failed to create NVRHI D3D12 swap-chain resources.";
            }
            return InitializeResult::Failure;
        }

        return InitializeResult::Success;
    }

    bool D3D12Backend::Resize(uint32_t width, uint32_t height)
    {
        if (!m_swapChain || width == 0 || height == 0 ||
            (width == m_width && height == m_height))
        {
            return true;
        }

        WaitForIdle();
        ReleaseSwapChainResources();

        if (FAILED(m_swapChain->ResizeBuffers(
            BufferCount,
            width,
            height,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            0)))
        {
            m_error = "IDXGISwapChain::ResizeBuffers failed.";
            return false;
        }

        m_width = width;
        m_height = height;
        return CreateSwapChainResources();
    }

    bool D3D12Backend::AcquireFrame(nvrhi::FramebufferHandle& framebuffer)
    {
        if (!m_swapChain)
        {
            return false;
        }

        framebuffer = m_framebuffers[m_swapChain->GetCurrentBackBufferIndex()];
        return framebuffer != nullptr;
    }

    void D3D12Backend::PrepareSubmit()
    {
    }

    bool D3D12Backend::Present()
    {
        if (!m_device->waitForIdle())
        {
            m_error = "NVRHI reported a D3D12 device failure while waiting for the frame.";
            return false;
        }

        const HRESULT result = m_swapChain->Present(1, 0);
        if (FAILED(result))
        {
            m_error = "IDXGISwapChain::Present failed.";
            return false;
        }

        return true;
    }

    void D3D12Backend::WaitForIdle()
    {
        if (m_device)
        {
            m_device->waitForIdle();
        }
    }

    bool D3D12Backend::CreateSwapChainResources()
    {
        for (uint32_t index = 0; index < BufferCount; ++index)
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            if (FAILED(m_swapChain->GetBuffer(index, IID_PPV_ARGS(&resource))))
            {
                m_error = "IDXGISwapChain::GetBuffer failed.";
                return false;
            }

            const auto textureDesc = nvrhi::TextureDesc()
                .setDimension(nvrhi::TextureDimension::Texture2D)
                .setWidth(m_width)
                .setHeight(m_height)
                .setFormat(nvrhi::Format::BGRA8_UNORM)
                .setIsRenderTarget(true)
                .setDebugName("D3D12 swap-chain image")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::Present);

            m_textures[index] = m_device->createHandleForNativeTexture(
                nvrhi::ObjectTypes::D3D12_Resource,
                nvrhi::Object(resource.Get()),
                textureDesc);
            if (!m_textures[index])
            {
                m_error = "NVRHI failed to wrap a D3D12 swap-chain image.";
                return false;
            }

            m_framebuffers[index] = m_device->createFramebuffer(
                nvrhi::FramebufferDesc().addColorAttachment(m_textures[index]));
            if (!m_framebuffers[index])
            {
                m_error = "NVRHI failed to create a D3D12 framebuffer.";
                return false;
            }
        }

        return true;
    }

    void D3D12Backend::ReleaseSwapChainResources()
    {
        for (auto& framebuffer : m_framebuffers)
        {
            framebuffer = nullptr;
        }
        for (auto& texture : m_textures)
        {
            texture = nullptr;
        }
        if (m_device)
        {
            m_device->runGarbageCollection();
        }
    }
}
