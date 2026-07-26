#include "ArdaBackendPch.h"

#include "ArdaBackendDevice.h"

#include <array>

namespace arda::backend
{
    namespace
    {
        class FArdaD3D12SwapChain final : public IArdaSwapChain
        {
        public:
            FArdaD3D12SwapChain(
                Microsoft::WRL::ComPtr<IDXGIFactory6> Factory,
                Microsoft::WRL::ComPtr<ID3D12CommandQueue> GraphicsQueue,
                nvrhi::DeviceHandle Device,
                HWND Window,
                uint32_t Width,
                uint32_t Height)
                : Factory(std::move(Factory))
                , GraphicsQueue(std::move(GraphicsQueue))
                , Device(std::move(Device))
                , Window(Window)
                , Width(Width)
                , Height(Height)
            {
            }

            ~FArdaD3D12SwapChain() override
            {
                WaitForIdle();
                ReleaseResources();
            }

            [[nodiscard]] bool Initialize()
            {
                DXGI_SWAP_CHAIN_DESC1 Description{};
                Description.Width = Width;
                Description.Height = Height;
                Description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                Description.SampleDesc.Count = 1;
                Description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                Description.BufferCount = BufferCount;
                Description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

                Microsoft::WRL::ComPtr<IDXGISwapChain1> BaseSwapChain;
                if (FAILED(Factory->CreateSwapChainForHwnd(
                    GraphicsQueue.Get(),
                    Window,
                    &Description,
                    nullptr,
                    nullptr,
                    &BaseSwapChain)))
                {
                    Error = "IDXGIFactory::CreateSwapChainForHwnd failed.";
                    return false;
                }

                Factory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER);
                if (FAILED(BaseSwapChain.As(&SwapChain)))
                {
                    Error = "The D3D12 swap chain does not expose IDXGISwapChain3.";
                    return false;
                }

                return CreateResources();
            }

            bool Resize(uint32_t NewWidth, uint32_t NewHeight) override
            {
                if (!SwapChain || NewWidth == 0 || NewHeight == 0 ||
                    (NewWidth == Width && NewHeight == Height))
                {
                    return true;
                }

                WaitForIdle();
                ReleaseResources();
                if (FAILED(SwapChain->ResizeBuffers(
                    BufferCount,
                    NewWidth,
                    NewHeight,
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    0)))
                {
                    Error = "IDXGISwapChain::ResizeBuffers failed.";
                    return false;
                }

                Width = NewWidth;
                Height = NewHeight;
                return CreateResources();
            }

            bool AcquireFrame(nvrhi::FramebufferHandle& OutFramebuffer) override
            {
                if (!SwapChain)
                {
                    Error = "The D3D12 swap chain is not initialized.";
                    OutFramebuffer = nullptr;
                    return false;
                }

                OutFramebuffer = Framebuffers[SwapChain->GetCurrentBackBufferIndex()];
                return OutFramebuffer != nullptr;
            }

            void PrepareSubmit() override
            {
            }

            bool Present() override
            {
                if (!Device || !SwapChain)
                {
                    Error = "The D3D12 swap chain is not initialized.";
                    return false;
                }
                if (!Device->waitForIdle())
                {
                    Error = "NVRHI reported a D3D12 device failure while waiting for the frame.";
                    return false;
                }
                if (FAILED(SwapChain->Present(1, 0)))
                {
                    Error = "IDXGISwapChain::Present failed.";
                    return false;
                }

                return true;
            }

            void WaitForIdle() noexcept override
            {
                if (Device)
                {
                    Device->waitForIdle();
                }
            }

            nvrhi::Format GetFormat() const noexcept override
            {
                return nvrhi::Format::BGRA8_UNORM;
            }

            uint32_t GetWidth() const noexcept override
            {
                return Width;
            }

            uint32_t GetHeight() const noexcept override
            {
                return Height;
            }

            const std::string& GetError() const noexcept override
            {
                return Error;
            }

        private:
            static constexpr uint32_t BufferCount = 2;

            bool CreateResources()
            {
                for (uint32_t Index = 0; Index < BufferCount; ++Index)
                {
                    Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
                    if (FAILED(SwapChain->GetBuffer(Index, IID_PPV_ARGS(&Resource))))
                    {
                        Error = "IDXGISwapChain::GetBuffer failed.";
                        ReleaseResources();
                        return false;
                    }

                    const auto Description = nvrhi::TextureDesc()
                        .setDimension(nvrhi::TextureDimension::Texture2D)
                        .setWidth(Width)
                        .setHeight(Height)
                        .setFormat(nvrhi::Format::BGRA8_UNORM)
                        .setIsRenderTarget(true)
                        .setDebugName("D3D12 swap-chain image")
                        .enableAutomaticStateTracking(nvrhi::ResourceStates::Present);

                    Textures[Index] = Device->createHandleForNativeTexture(
                        nvrhi::ObjectTypes::D3D12_Resource,
                        nvrhi::Object(Resource.Get()),
                        Description);
                    if (!Textures[Index])
                    {
                        Error = "NVRHI failed to wrap a D3D12 swap-chain image.";
                        ReleaseResources();
                        return false;
                    }

                    Framebuffers[Index] = Device->createFramebuffer(
                        nvrhi::FramebufferDesc().addColorAttachment(Textures[Index]));
                    if (!Framebuffers[Index])
                    {
                        Error = "NVRHI failed to create a D3D12 framebuffer.";
                        ReleaseResources();
                        return false;
                    }
                }

                Error.clear();
                return true;
            }

            void ReleaseResources()
            {
                for (auto& Framebuffer : Framebuffers)
                {
                    Framebuffer = nullptr;
                }
                for (auto& Texture : Textures)
                {
                    Texture = nullptr;
                }
                if (Device)
                {
                    Device->runGarbageCollection();
                }
            }

            Microsoft::WRL::ComPtr<IDXGIFactory6> Factory;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> GraphicsQueue;
            nvrhi::DeviceHandle Device;
            HWND Window = nullptr;
            uint32_t Width = 0;
            uint32_t Height = 0;
            std::string Error;
            Microsoft::WRL::ComPtr<IDXGISwapChain3> SwapChain;
            std::array<nvrhi::TextureHandle, BufferCount> Textures;
            std::array<nvrhi::FramebufferHandle, BufferCount> Framebuffers;
        };

        class FArdaD3D12BackendDevice final : public IArdaBackendDevice
        {
        public:
            ~FArdaD3D12BackendDevice() override
            {
                WaitForIdle();
                Device = nullptr;
                NativeDevice = nullptr;
            }

            EArdaInitializeResult Initialize(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface) override
            {
                if (WindowSurface)
                {
                    Window = WindowSurface->GetD3D12WindowHandle();
                    if (!Window)
                    {
                        Error = "The window surface did not provide a Win32 window handle.";
                        return EArdaInitializeResult::Failure;
                    }
                }

                if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory))))
                {
                    Error = "CreateDXGIFactory2 failed; D3D12 is unavailable.";
                    return EArdaInitializeResult::Unavailable;
                }

                Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
                for (UINT Index = 0;
                     Factory->EnumAdapterByGpuPreference(
                         Index,
                         DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                         IID_PPV_ARGS(&Adapter)) != DXGI_ERROR_NOT_FOUND;
                     ++Index)
                {
                    DXGI_ADAPTER_DESC1 Description{};
                    Adapter->GetDesc1(&Description);
                    if ((Description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                        SUCCEEDED(D3D12CreateDevice(
                            Adapter.Get(),
                            D3D_FEATURE_LEVEL_12_0,
                            __uuidof(ID3D12Device),
                            nullptr)))
                    {
                        break;
                    }
                    Adapter.Reset();
                }

                if (!Adapter && FAILED(Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter))))
                {
                    Error = "No D3D12 adapter or WARP adapter is available.";
                    return EArdaInitializeResult::Unavailable;
                }

                if (FAILED(D3D12CreateDevice(
                    Adapter.Get(),
                    D3D_FEATURE_LEVEL_12_0,
                    IID_PPV_ARGS(&D3DDevice))))
                {
                    Error = "D3D12CreateDevice failed.";
                    return EArdaInitializeResult::Unavailable;
                }

                D3D12_COMMAND_QUEUE_DESC QueueDescription{};
                QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                if (FAILED(D3DDevice->CreateCommandQueue(
                    &QueueDescription,
                    IID_PPV_ARGS(&GraphicsQueue))))
                {
                    Error = "ID3D12Device::CreateCommandQueue failed.";
                    return EArdaInitializeResult::Failure;
                }

                nvrhi::d3d12::DeviceDesc Description;
                Description.errorCB = Configuration.messageCallback;
                Description.pDevice = D3DDevice.Get();
                Description.pGraphicsCommandQueue = GraphicsQueue.Get();
                NativeDevice = nvrhi::d3d12::createDevice(Description);
                if (!NativeDevice)
                {
                    Error = "nvrhi::d3d12::createDevice failed.";
                    return EArdaInitializeResult::Failure;
                }

                Device = Configuration.enableValidation
                    ? nvrhi::validation::createValidationLayer(NativeDevice)
                    : nvrhi::DeviceHandle(NativeDevice);
                if (!Device)
                {
                    Error = "Failed to create the NVRHI D3D12 device.";
                    return EArdaInitializeResult::Failure;
                }

                Error.clear();
                return EArdaInitializeResult::Success;
            }

            std::unique_ptr<IArdaSwapChain> CreateSwapChain(
                uint32_t Width,
                uint32_t Height) override
            {
                if (!Window)
                {
                    Error = "D3D12 presentation was not initialized with a window surface.";
                    return nullptr;
                }

                auto SwapChain = std::make_unique<FArdaD3D12SwapChain>(
                    Factory,
                    GraphicsQueue,
                    Device,
                    Window,
                    Width,
                    Height);
                if (!SwapChain->Initialize())
                {
                    Error = SwapChain->GetError();
                    return nullptr;
                }

                return SwapChain;
            }

            void WaitForIdle() noexcept override
            {
                if (Device)
                {
                    Device->waitForIdle();
                }
            }

            nvrhi::DeviceHandle GetDevice() const noexcept override
            {
                return Device;
            }

            const std::string& GetError() const noexcept override
            {
                return Error;
            }

        private:
            HWND Window = nullptr;
            std::string Error;
            Microsoft::WRL::ComPtr<IDXGIFactory6> Factory;
            Microsoft::WRL::ComPtr<ID3D12Device> D3DDevice;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> GraphicsQueue;
            nvrhi::d3d12::DeviceHandle NativeDevice;
            nvrhi::DeviceHandle Device;
        };
    }

    std::unique_ptr<IArdaBackendDevice> CreateD3D12BackendDevice()
    {
        return std::make_unique<FArdaD3D12BackendDevice>();
    }
}
