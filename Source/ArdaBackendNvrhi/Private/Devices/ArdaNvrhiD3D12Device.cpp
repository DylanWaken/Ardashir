#include "ArdaNvrhiPch.h"

#include "ArdaNvrhiBackendDevice.h"
#include "Native/ArdaNvrhiPipelineCache.h"
#include "RHI/ArdaNvrhiDevice.h"

#include <array>

namespace arda::backend
{
    namespace
    {
        struct FArdaD3D12Lifetime
        {
            FArdaNvrhiMessageCallback mMessageCallback;
            Microsoft::WRL::ComPtr<IDXGIFactory6> mFactory;
            Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> mGraphicsQueue;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> mComputeQueue;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCopyQueue;
        };

        class FArdaD3D12SwapChain final : public IArdaSwapChain
        {
        public:
            FArdaD3D12SwapChain(
                Microsoft::WRL::ComPtr<IDXGIFactory6> Factory,
                Microsoft::WRL::ComPtr<ID3D12CommandQueue> GraphicsQueue,
                nvrhi::DeviceHandle Device,
                rhi::FArdaRHIDeviceRef ArdaDevice,
                HWND Window,
                uint32_t Width,
                uint32_t Height)
                : mFactory(eastl::move(Factory))
                , mGraphicsQueue(eastl::move(GraphicsQueue))
                , mDevice(eastl::move(Device))
                , mArdaDevice(eastl::move(ArdaDevice))
                , mWindow(Window)
                , mWidth(Width)
                , mHeight(Height)
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
                Description.Width = mWidth;
                Description.Height = mHeight;
                Description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                Description.SampleDesc.Count = 1;
                Description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                Description.BufferCount = mBufferCount;
                Description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

                Microsoft::WRL::ComPtr<IDXGISwapChain1> BaseSwapChain;
                if (FAILED(mFactory->CreateSwapChainForHwnd(
                    mGraphicsQueue.Get(),
                    mWindow,
                    &Description,
                    nullptr,
                    nullptr,
                    &BaseSwapChain)))
                {
                    mError = "IDXGIFactory::CreateSwapChainForHwnd failed.";
                    return false;
                }

                mFactory->MakeWindowAssociation(mWindow, DXGI_MWA_NO_ALT_ENTER);
                if (FAILED(BaseSwapChain.As(&mSwapChain)))
                {
                    mError = "The D3D12 swap chain does not expose IDXGISwapChain3.";
                    return false;
                }

                return CreateResources();
            }

            bool Resize(uint32_t NewWidth, uint32_t NewHeight) override
            {
                if (!mSwapChain || NewWidth == 0 || NewHeight == 0 ||
                    (NewWidth == mWidth && NewHeight == mHeight))
                {
                    return true;
                }

                WaitForIdle();
                ReleaseResources();
                if (FAILED(mSwapChain->ResizeBuffers(
                    mBufferCount,
                    NewWidth,
                    NewHeight,
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    0)))
                {
                    mError = "IDXGISwapChain::ResizeBuffers failed.";
                    return false;
                }

                mWidth = NewWidth;
                mHeight = NewHeight;
                return CreateResources();
            }

            bool AcquireFrame(rhi::FArdaRHIFramebufferRef& OutFramebuffer) override
            {
                if (!mSwapChain)
                {
                    mError = "The D3D12 swap chain is not initialized.";
                    OutFramebuffer = nullptr;
                    return false;
                }

                OutFramebuffer = mArdaFramebuffers[mSwapChain->GetCurrentBackBufferIndex()];
                return static_cast<bool>(OutFramebuffer);
            }

            void PrepareSubmit() override
            {
            }

            bool Present() override
            {
                if (!mDevice || !mSwapChain)
                {
                    mError = "The D3D12 swap chain is not initialized.";
                    return false;
                }
                if (FAILED(mSwapChain->Present(1, 0)))
                {
                    mError = "IDXGISwapChain::Present failed.";
                    return false;
                }

                return true;
            }

            void WaitForIdle() noexcept override
            {
                if (mDevice)
                {
                    mDevice->waitForIdle();
                }
            }

            rhi::EArdaRHIFormat GetFormat() const noexcept override
            {
                return rhi::EArdaRHIFormat::BGRA8UNorm;
            }

            uint32_t GetWidth() const noexcept override
            {
                return mWidth;
            }

            uint32_t GetHeight() const noexcept override
            {
                return mHeight;
            }

            const eastl::string& GetError() const noexcept override
            {
                return mError;
            }

        private:
            static constexpr uint32_t mBufferCount = 2;

            bool CreateResources()
            {
                for (uint32_t Index = 0; Index < mBufferCount; ++Index)
                {
                    Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
                    if (FAILED(mSwapChain->GetBuffer(Index, IID_PPV_ARGS(&Resource))))
                    {
                        mError = "IDXGISwapChain::GetBuffer failed.";
                        ReleaseResources();
                        return false;
                    }

                    rhi::FArdaRHINativeTextureImportDesc ImportDesc;
                    ImportDesc.mNativeObject =
                        reinterpret_cast<uintptr_t>(Resource.Get());
                    ImportDesc.mNativeType =
                        rhi::EArdaRHINativeResourceType::D3D12Resource;
                    ImportDesc.mOwnership =
                        rhi::EArdaRHINativeOwnership::Borrowed;
                    ImportDesc.mInitialState =
                        rhi::EArdaRHIResourceState::Present;
                    ImportDesc.mTexture.mWidth = mWidth;
                    ImportDesc.mTexture.mHeight = mHeight;
                    ImportDesc.mTexture.mFormat =
                        rhi::EArdaRHIFormat::BGRA8UNorm;
                    ImportDesc.mTexture.mUsage =
                        rhi::EArdaRHITextureUsage::RenderTarget;
                    ImportDesc.mTexture.mInitialState =
                        rhi::EArdaRHIResourceState::Present;
                    ImportDesc.mTexture.mbKeepInitialState = true;
                    ImportDesc.mTexture.mDebugName =
                        "D3D12 swap-chain image";
                    auto ArdaTexture =
                        mArdaDevice->ImportNativeTexture(ImportDesc);
                    if (!ArdaTexture)
                    {
                        mError = ArdaTexture.mStatus.mMessage;
                        ReleaseResources();
                        return false;
                    }
                    rhi::FArdaRHIFramebufferDesc ArdaFramebufferDesc;
                    ArdaFramebufferDesc.mColorAttachments.push_back(
                        { ArdaTexture.mValue, {} });
                    auto ArdaFramebuffer =
                        mArdaDevice->CreateFramebuffer(ArdaFramebufferDesc);
                    if (!ArdaFramebuffer)
                    {
                        mError = ArdaFramebuffer.mStatus.mMessage;
                        ReleaseResources();
                        return false;
                    }
                    mArdaFramebuffers[Index] =
                        eastl::move(ArdaFramebuffer.mValue);
                }

                mError.clear();
                return true;
            }

            void ReleaseResources()
            {
                for (auto& Framebuffer : mArdaFramebuffers)
                    Framebuffer = nullptr;
                if (mArdaDevice)
                {
                    mArdaDevice->TrimDescriptorCaches();
                }
                if (mDevice)
                {
                    mDevice->runGarbageCollection();
                }
            }

            Microsoft::WRL::ComPtr<IDXGIFactory6> mFactory;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> mGraphicsQueue;
            nvrhi::DeviceHandle mDevice;
            rhi::FArdaRHIDeviceRef mArdaDevice;
            HWND mWindow = nullptr;
            uint32_t mWidth = 0;
            uint32_t mHeight = 0;
            eastl::string mError;
            Microsoft::WRL::ComPtr<IDXGISwapChain3> mSwapChain;
            std::array<rhi::FArdaRHIFramebufferRef, mBufferCount> mArdaFramebuffers;
        };

        class FArdaD3D12BackendDevice final : public IArdaBackendDevice
        {
        public:
            ~FArdaD3D12BackendDevice() override
            {
                WaitForIdle();
                mArdaDevice = nullptr;
                mDevice = nullptr;
                mNativeDevice = nullptr;
                mPipelineCache.reset();
                mLifetime.reset();
            }

            EArdaInitializeResult Initialize(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface,
                const IArdaExternalDeviceProvider*) override
            {
                if (WindowSurface)
                {
                    mWindow = WindowSurface->GetD3D12WindowHandle().As<HWND>();
                    if (!mWindow)
                    {
                        mError = "The window surface did not provide a Win32 window handle.";
                        return EArdaInitializeResult::Failure;
                    }
                }

                if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&mFactory))))
                {
                    mError = "CreateDXGIFactory2 failed; D3D12 is unavailable.";
                    return EArdaInitializeResult::Unavailable;
                }

                Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
                for (UINT Index = 0;
                     mFactory->EnumAdapterByGpuPreference(
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

                if (!Adapter && FAILED(mFactory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter))))
                {
                    mError = "No D3D12 adapter or WARP adapter is available.";
                    return EArdaInitializeResult::Unavailable;
                }

                if (FAILED(D3D12CreateDevice(
                    Adapter.Get(),
                    D3D_FEATURE_LEVEL_12_0,
                    IID_PPV_ARGS(&mD3DDevice))))
                {
                    mError = "D3D12CreateDevice failed.";
                    return EArdaInitializeResult::Unavailable;
                }

                D3D12_COMMAND_QUEUE_DESC QueueDescription{};
                QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                if (FAILED(mD3DDevice->CreateCommandQueue(
                    &QueueDescription,
                    IID_PPV_ARGS(&mGraphicsQueue))))
                {
                    mError = "ID3D12Device::CreateCommandQueue failed for the graphics queue.";
                    return EArdaInitializeResult::Failure;
                }

                QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
                if (FAILED(mD3DDevice->CreateCommandQueue(
                    &QueueDescription,
                    IID_PPV_ARGS(&mComputeQueue))))
                {
                    mError = "ID3D12Device::CreateCommandQueue failed for the compute queue.";
                    return EArdaInitializeResult::Failure;
                }

                QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_COPY;
                if (FAILED(mD3DDevice->CreateCommandQueue(
                    &QueueDescription,
                    IID_PPV_ARGS(&mCopyQueue))))
                {
                    mError = "ID3D12Device::CreateCommandQueue failed for the copy queue.";
                    return EArdaInitializeResult::Failure;
                }

                mLifetime = eastl::make_shared<FArdaD3D12Lifetime>();
                mLifetime->mFactory = mFactory;
                mLifetime->mDevice = mD3DDevice;
                mLifetime->mGraphicsQueue = mGraphicsQueue;
                mLifetime->mComputeQueue = mComputeQueue;
                mLifetime->mCopyQueue = mCopyQueue;
                mLifetime->mMessageCallback.SetTarget(
                    Configuration.mMessageCallback);
                mPipelineCache =
                    rhi::private_impl::CreateArdaNvrhiD3D12PipelineCache(
                        mLifetime->mDevice.Get(),
                        Configuration.mBackendName,
                        Configuration.mPipelineCacheDirectory,
                        Configuration.mMessageCallback);

                nvrhi::d3d12::DeviceDesc Description;
                Description.errorCB = &mLifetime->mMessageCallback;
                Description.pDevice = mPipelineCache
                    ? mPipelineCache->GetD3D12DeviceForNvrhi()
                    : mLifetime->mDevice.Get();
                Description.pGraphicsCommandQueue =
                    mLifetime->mGraphicsQueue.Get();
                Description.pComputeCommandQueue =
                    mLifetime->mComputeQueue.Get();
                Description.pCopyCommandQueue =
                    mLifetime->mCopyQueue.Get();
                mNativeDevice = nvrhi::d3d12::createDevice(Description);
                if (!mNativeDevice)
                {
                    mError = "nvrhi::d3d12::createDevice failed.";
                    return EArdaInitializeResult::Failure;
                }

                mDevice = Configuration.mbEnableValidation
                    ? nvrhi::validation::createValidationLayer(mNativeDevice)
                    : nvrhi::DeviceHandle(mNativeDevice);
                if (!mDevice)
                {
                    mError = "Failed to create the NVRHI D3D12 device.";
                    return EArdaInitializeResult::Failure;
                }
                mArdaDevice = rhi::private_impl::CreateArdaNvrhiDevice(
                    mDevice, mLifetime, mPipelineCache);
                if (!mArdaDevice)
                {
                    mError = "Failed to create the opaque Arda D3D12 device.";
                    return EArdaInitializeResult::Failure;
                }

                mQueueCapabilities.mbGraphics = true;
                mQueueCapabilities.mbCompute =
                    mDevice->queryFeatureSupport(nvrhi::Feature::ComputeQueue);
                mQueueCapabilities.mbCopy =
                    mDevice->queryFeatureSupport(nvrhi::Feature::CopyQueue);
                mError.clear();
                return EArdaInitializeResult::Success;
            }

            eastl::unique_ptr<IArdaSwapChain> CreateSwapChain(
                uint32_t Width,
                uint32_t Height) override
            {
                if (!mWindow)
                {
                    mError = "D3D12 presentation was not initialized with a window surface.";
                    return nullptr;
                }

                auto SwapChain = eastl::make_unique<FArdaD3D12SwapChain>(
                    mFactory,
                    mGraphicsQueue,
                    mDevice,
                    mArdaDevice,
                    mWindow,
                    Width,
                    Height);
                if (!SwapChain->Initialize())
                {
                    mError = SwapChain->GetError();
                    return nullptr;
                }

                return SwapChain;
            }

            void WaitForIdle() noexcept override
            {
                if (mDevice)
                {
                    mDevice->waitForIdle();
                }
            }

            rhi::FArdaRHIDeviceRef GetDevice() const noexcept override
            {
                return mArdaDevice;
            }

            FArdaQueueCapabilities GetQueueCapabilities() const noexcept override
            {
                return mQueueCapabilities;
            }

            const eastl::string& GetError() const noexcept override
            {
                return mError;
            }

        private:
            HWND mWindow = nullptr;
            eastl::string mError;
            eastl::shared_ptr<FArdaD3D12Lifetime> mLifetime;
            eastl::shared_ptr<rhi::private_impl::IArdaNvrhiPipelineCache>
                mPipelineCache;
            Microsoft::WRL::ComPtr<IDXGIFactory6> mFactory;
            Microsoft::WRL::ComPtr<ID3D12Device> mD3DDevice;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> mGraphicsQueue;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> mComputeQueue;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCopyQueue;
            nvrhi::d3d12::DeviceHandle mNativeDevice;
            nvrhi::DeviceHandle mDevice;
            rhi::FArdaRHIDeviceRef mArdaDevice;
            FArdaQueueCapabilities mQueueCapabilities;
        };
    }

    eastl::unique_ptr<IArdaBackendDevice> CreateD3D12BackendDevice()
    {
        return eastl::make_unique<FArdaD3D12BackendDevice>();
    }
}
