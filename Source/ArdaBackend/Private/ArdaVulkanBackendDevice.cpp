#include "ArdaBackendPch.h"

#include "ArdaBackendDevice.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace arda::backend
{
    namespace
    {
        vk::SurfaceFormatKHR SelectSurfaceFormat(
            const std::vector<vk::SurfaceFormatKHR>& Formats)
        {
            for (const auto& Format : Formats)
            {
                if (Format.format == vk::Format::eB8G8R8A8Unorm &&
                    Format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                {
                    return Format;
                }
            }
            return Formats.front();
        }

        vk::CompositeAlphaFlagBitsKHR SelectCompositeAlpha(
            vk::CompositeAlphaFlagsKHR Supported)
        {
            constexpr vk::CompositeAlphaFlagBitsKHR Candidates[] = {
                vk::CompositeAlphaFlagBitsKHR::eOpaque,
                vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
                vk::CompositeAlphaFlagBitsKHR::eInherit
            };

            for (const auto Candidate : Candidates)
            {
                if (Supported & Candidate)
                {
                    return Candidate;
                }
            }
            return vk::CompositeAlphaFlagBitsKHR::eOpaque;
        }

        VkSurfaceKHR DecodeVulkanSurface(nvrhi::Object Surface)
        {
#if VK_USE_64_BIT_PTR_DEFINES
            return Surface;
#else
            return static_cast<VkSurfaceKHR>(Surface.integer);
#endif
        }

        class FArdaVulkanSwapChain final : public IArdaSwapChain
        {
        public:
            FArdaVulkanSwapChain(
                vk::PhysicalDevice PhysicalDevice,
                vk::Device VulkanDevice,
                vk::Queue GraphicsQueue,
                vk::SurfaceKHR Surface,
                nvrhi::vulkan::DeviceHandle NativeDevice,
                nvrhi::DeviceHandle Device,
                uint32_t Width,
                uint32_t Height)
                : mPhysicalDevice(PhysicalDevice)
                , mVulkanDevice(VulkanDevice)
                , mGraphicsQueue(GraphicsQueue)
                , mSurface(Surface)
                , mNativeDevice(std::move(NativeDevice))
                , mDevice(std::move(Device))
                , mWidth(Width)
                , mHeight(Height)
            {
            }

            ~FArdaVulkanSwapChain() override
            {
                WaitForIdle();
                ReleaseSwapChain();
                if (mVulkanDevice)
                {
                    for (const auto Semaphore : mImageAvailable)
                    {
                        if (Semaphore)
                        {
                            mVulkanDevice.destroySemaphore(Semaphore);
                        }
                    }
                    for (const auto Semaphore : mRenderFinished)
                    {
                        if (Semaphore)
                        {
                            mVulkanDevice.destroySemaphore(Semaphore);
                        }
                    }
                }
            }

            [[nodiscard]] bool Initialize()
            {
                return CreateFrameSyncObjects() && CreateSwapChain(mWidth, mHeight);
            }

            bool Resize(uint32_t NewWidth, uint32_t NewHeight) override
            {
                if (NewWidth == 0 || NewHeight == 0 ||
                    (NewWidth == mWidth && NewHeight == mHeight))
                {
                    return true;
                }

                WaitForIdle();
                ReleaseSwapChain();
                return CreateSwapChain(NewWidth, NewHeight);
            }

            bool AcquireFrame(nvrhi::FramebufferHandle& OutFramebuffer) override
            {
                OutFramebuffer = nullptr;
                if (!mSwapChain)
                {
                    mError = "The Vulkan swap chain is not initialized.";
                    return false;
                }

                try
                {
                    const auto Result = mVulkanDevice.acquireNextImageKHR(
                        mSwapChain,
                        std::numeric_limits<uint64_t>::max(),
                        mImageAvailable[mFrameIndex]);
                    if (Result.result != vk::Result::eSuccess &&
                        Result.result != vk::Result::eSuboptimalKHR)
                    {
                        mError = "vkAcquireNextImageKHR failed.";
                        return false;
                    }

                    mImageIndex = Result.value;
                    mNativeDevice->queueWaitForSemaphore(
                        nvrhi::CommandQueue::Graphics,
                        mImageAvailable[mFrameIndex],
                        0);
                    OutFramebuffer = mFramebuffers[mImageIndex];
                    return OutFramebuffer != nullptr;
                }
                catch (const vk::OutOfDateKHRError&)
                {
                    if (!RecreateSwapChain())
                    {
                        return false;
                    }
                    return AcquireFrame(OutFramebuffer);
                }
                catch (const vk::SystemError& Exception)
                {
                    mError = std::string("Vulkan image acquisition failed: ") + Exception.what();
                    return false;
                }
            }

            void PrepareSubmit() override
            {
                if (mNativeDevice)
                {
                    mNativeDevice->queueSignalSemaphore(
                        nvrhi::CommandQueue::Graphics,
                        mRenderFinished[mFrameIndex],
                        0);
                }
            }

            bool Present() override
            {
                if (!mSwapChain)
                {
                    mError = "The Vulkan swap chain is not initialized.";
                    return false;
                }

                try
                {
                    const vk::Semaphore WaitSemaphore = mRenderFinished[mFrameIndex];
                    vk::PresentInfoKHR Description;
                    Description.setWaitSemaphores(WaitSemaphore);
                    Description.setSwapchains(mSwapChain);
                    Description.setImageIndices(mImageIndex);

                    const vk::Result Result = mGraphicsQueue.presentKHR(Description);
                    if (Result != vk::Result::eSuccess &&
                        Result != vk::Result::eSuboptimalKHR)
                    {
                        mError = "vkQueuePresentKHR failed.";
                        return false;
                    }

                    mFrameIndex = (mFrameIndex + 1) % mFramesInFlight;
                    return true;
                }
                catch (const vk::OutOfDateKHRError&)
                {
                    return RecreateSwapChain();
                }
                catch (const vk::SystemError& Exception)
                {
                    mError = std::string("Vulkan presentation failed: ") + Exception.what();
                    return false;
                }
            }

            void WaitForIdle() noexcept override
            {
                if (mDevice)
                {
                    mDevice->waitForIdle();
                }
                if (mVulkanDevice)
                {
                    try
                    {
                        mVulkanDevice.waitIdle();
                    }
                    catch (const vk::SystemError&)
                    {
                    }
                }
            }

            nvrhi::Format GetFormat() const noexcept override
            {
                return nvrhi::Format::BGRA8_UNORM;
            }

            uint32_t GetWidth() const noexcept override
            {
                return mWidth;
            }

            uint32_t GetHeight() const noexcept override
            {
                return mHeight;
            }

            const std::string& GetError() const noexcept override
            {
                return mError;
            }

        private:
            static constexpr uint32_t mFramesInFlight = 2;

            bool CreateSwapChain(uint32_t RequestedWidth, uint32_t RequestedHeight)
            {
                try
                {
                    const auto Capabilities =
                        mPhysicalDevice.getSurfaceCapabilitiesKHR(mSurface);
                    const auto Formats = mPhysicalDevice.getSurfaceFormatsKHR(mSurface);
                    if (Formats.empty())
                    {
                        mError = "The Vulkan surface exposes no formats.";
                        return false;
                    }

                    const auto Format = SelectSurfaceFormat(Formats);
                    if (Format.format != vk::Format::eB8G8R8A8Unorm)
                    {
                        mError = "The Vulkan surface does not support BGRA8_UNORM.";
                        return false;
                    }

                    vk::Extent2D Extent;
                    if (Capabilities.currentExtent.width !=
                        std::numeric_limits<uint32_t>::max())
                    {
                        Extent = Capabilities.currentExtent;
                    }
                    else
                    {
                        Extent.width = std::clamp(
                            RequestedWidth,
                            Capabilities.minImageExtent.width,
                            Capabilities.maxImageExtent.width);
                        Extent.height = std::clamp(
                            RequestedHeight,
                            Capabilities.minImageExtent.height,
                            Capabilities.maxImageExtent.height);
                    }

                    uint32_t ImageCount = Capabilities.minImageCount + 1;
                    if (Capabilities.maxImageCount > 0)
                    {
                        ImageCount = std::min(ImageCount, Capabilities.maxImageCount);
                    }

                    vk::SwapchainCreateInfoKHR Description;
                    Description.setSurface(mSurface);
                    Description.setMinImageCount(ImageCount);
                    Description.setImageFormat(Format.format);
                    Description.setImageColorSpace(Format.colorSpace);
                    Description.setImageExtent(Extent);
                    Description.setImageArrayLayers(1);
                    Description.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment);
                    Description.setImageSharingMode(vk::SharingMode::eExclusive);
                    Description.setPreTransform(Capabilities.currentTransform);
                    Description.setCompositeAlpha(
                        SelectCompositeAlpha(Capabilities.supportedCompositeAlpha));
                    Description.setPresentMode(vk::PresentModeKHR::eFifo);
                    Description.setClipped(true);

                    mSwapChain = mVulkanDevice.createSwapchainKHR(Description);
                    const auto Images = mVulkanDevice.getSwapchainImagesKHR(mSwapChain);
                    mWidth = Extent.width;
                    mHeight = Extent.height;
                    mTextures.resize(Images.size());
                    mFramebuffers.resize(Images.size());

                    for (size_t Index = 0; Index < Images.size(); ++Index)
                    {
                        const auto TextureDescription = nvrhi::TextureDesc()
                            .setDimension(nvrhi::TextureDimension::Texture2D)
                            .setWidth(mWidth)
                            .setHeight(mHeight)
                            .setFormat(nvrhi::Format::BGRA8_UNORM)
                            .setIsRenderTarget(true)
                            .setDebugName("Vulkan swap-chain image")
                            .enableAutomaticStateTracking(nvrhi::ResourceStates::Present);

                        const auto NativeImage =
                            reinterpret_cast<uint64_t>(static_cast<VkImage>(Images[Index]));
                        mTextures[Index] = mDevice->createHandleForNativeTexture(
                            nvrhi::ObjectTypes::VK_Image,
                            nvrhi::Object(NativeImage),
                            TextureDescription);
                        if (!mTextures[Index])
                        {
                            mError = "NVRHI failed to wrap a Vulkan swap-chain image.";
                            ReleaseSwapChain();
                            return false;
                        }

                        mFramebuffers[Index] = mDevice->createFramebuffer(
                            nvrhi::FramebufferDesc().addColorAttachment(mTextures[Index]));
                        if (!mFramebuffers[Index])
                        {
                            mError = "NVRHI failed to create a Vulkan framebuffer.";
                            ReleaseSwapChain();
                            return false;
                        }
                    }
                }
                catch (const vk::SystemError& Exception)
                {
                    mError = std::string("Vulkan swap-chain creation failed: ") + Exception.what();
                    ReleaseSwapChain();
                    return false;
                }

                mError.clear();
                return true;
            }

            void ReleaseSwapChain()
            {
                mFramebuffers.clear();
                mTextures.clear();
                if (mDevice)
                {
                    mDevice->runGarbageCollection();
                }
                if (mSwapChain && mVulkanDevice)
                {
                    mVulkanDevice.destroySwapchainKHR(mSwapChain);
                    mSwapChain = nullptr;
                }
            }

            bool CreateFrameSyncObjects()
            {
                try
                {
                    const vk::SemaphoreCreateInfo Description;
                    for (uint32_t Index = 0; Index < mFramesInFlight; ++Index)
                    {
                        mImageAvailable[Index] = mVulkanDevice.createSemaphore(Description);
                        mRenderFinished[Index] = mVulkanDevice.createSemaphore(Description);
                    }
                }
                catch (const vk::SystemError& Exception)
                {
                    mError = std::string("Vulkan semaphore creation failed: ") + Exception.what();
                    return false;
                }

                return true;
            }

            bool RecreateSwapChain()
            {
                WaitForIdle();
                ReleaseSwapChain();
                return CreateSwapChain(mWidth, mHeight);
            }

            vk::PhysicalDevice mPhysicalDevice;
            vk::Device mVulkanDevice;
            vk::Queue mGraphicsQueue;
            vk::SurfaceKHR mSurface;
            nvrhi::vulkan::DeviceHandle mNativeDevice;
            nvrhi::DeviceHandle mDevice;
            uint32_t mWidth = 0;
            uint32_t mHeight = 0;
            uint32_t mImageIndex = 0;
            uint32_t mFrameIndex = 0;
            std::string mError;
            vk::SwapchainKHR mSwapChain;
            std::vector<nvrhi::TextureHandle> mTextures;
            std::vector<nvrhi::FramebufferHandle> mFramebuffers;
            std::array<vk::Semaphore, mFramesInFlight> mImageAvailable;
            std::array<vk::Semaphore, mFramesInFlight> mRenderFinished;
        };

        class FArdaVulkanBackendDevice final : public IArdaBackendDevice
        {
        public:
            ~FArdaVulkanBackendDevice() override
            {
                WaitForIdle();
                mDevice = nullptr;
                mNativeDevice = nullptr;

                if (mVulkanDevice)
                {
                    mVulkanDevice.destroy();
                }
                if (mSurface && mInstance)
                {
                    mInstance.destroySurfaceKHR(mSurface);
                }
                if (mInstance)
                {
                    mInstance.destroy();
                }
            }

            EArdaInitializeResult Initialize(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface) override
            {
                try
                {
                    const auto GetInstanceProcAddress =
                        mLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
                    if (!GetInstanceProcAddress)
                    {
                        mError = "The Vulkan loader is not installed.";
                        return EArdaInitializeResult::Unavailable;
                    }
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(GetInstanceProcAddress);

                    if (WindowSurface)
                    {
                        mInstanceExtensions = WindowSurface->GetVulkanInstanceExtensions();
                        if (mInstanceExtensions.empty())
                        {
                            mError =
                                "The window surface did not provide Vulkan instance extensions.";
                            return EArdaInitializeResult::Unavailable;
                        }
                    }

                    const vk::ApplicationInfo ApplicationInfo(
                        "Ardashir",
                        VK_MAKE_VERSION(0, 1, 0),
                        "Ardashir",
                        VK_MAKE_VERSION(0, 1, 0),
                        VK_API_VERSION_1_3);
                    vk::InstanceCreateInfo InstanceDescription;
                    InstanceDescription.setPApplicationInfo(&ApplicationInfo);
                    InstanceDescription.setPEnabledExtensionNames(mInstanceExtensions);
                    mInstance = vk::createInstance(InstanceDescription);
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(mInstance);

                    if (WindowSurface)
                    {
                        std::string SurfaceError;
                        const nvrhi::Object SurfaceObject = WindowSurface->CreateVulkanSurface(
                            nvrhi::Object(static_cast<VkInstance>(mInstance)),
                            SurfaceError);
                        const VkSurfaceKHR NativeSurface = DecodeVulkanSurface(SurfaceObject);
                        if (NativeSurface == VK_NULL_HANDLE)
                        {
                            mError = SurfaceError.empty()
                                ? "The window surface failed to create a Vulkan surface."
                                : SurfaceError;
                            return EArdaInitializeResult::Unavailable;
                        }
                        mSurface = NativeSurface;
                    }

                    if (!SelectPhysicalDevice())
                    {
                        mError = mSurface
                            ? "No Vulkan 1.3 device with required graphics and presentation "
                              "features is available."
                            : "No Vulkan 1.3 device with graphics, dynamic rendering, "
                              "synchronization2, and timeline semaphore support is available.";
                        return EArdaInitializeResult::Unavailable;
                    }

                    const float QueuePriority = 1.f;
                    vk::DeviceQueueCreateInfo QueueDescription;
                    QueueDescription.setQueueFamilyIndex(mGraphicsQueueFamily);
                    QueueDescription.setQueueCount(1);
                    QueueDescription.setPQueuePriorities(&QueuePriority);

                    vk::PhysicalDeviceVulkan13Features Features13;
                    Features13.setDynamicRendering(true);
                    Features13.setSynchronization2(true);

                    vk::PhysicalDeviceVulkan12Features Features12;
                    Features12.setTimelineSemaphore(true);
                    Features12.setPNext(&Features13);

                    std::vector<const char*> DeviceExtensions;
                    if (mSurface)
                    {
                        DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
                    }

                    vk::DeviceCreateInfo DeviceDescription;
                    DeviceDescription.setPNext(&Features12);
                    DeviceDescription.setQueueCreateInfos(QueueDescription);
                    DeviceDescription.setPEnabledExtensionNames(DeviceExtensions);
                    mVulkanDevice = mPhysicalDevice.createDevice(DeviceDescription);
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(mVulkanDevice);
                    mGraphicsQueue = mVulkanDevice.getQueue(mGraphicsQueueFamily, 0);

                    nvrhi::vulkan::DeviceDesc Description;
                    Description.errorCB = Configuration.mMessageCallback;
                    Description.instance = mInstance;
                    Description.physicalDevice = mPhysicalDevice;
                    Description.device = mVulkanDevice;
                    Description.graphicsQueue = mGraphicsQueue;
                    Description.graphicsQueueIndex =
                        static_cast<int>(mGraphicsQueueFamily);
                    Description.instanceExtensions = mInstanceExtensions.data();
                    Description.numInstanceExtensions =
                        static_cast<uint32_t>(mInstanceExtensions.size());
                    Description.deviceExtensions = DeviceExtensions.data();
                    Description.numDeviceExtensions =
                        static_cast<uint32_t>(DeviceExtensions.size());
                    mNativeDevice = nvrhi::vulkan::createDevice(Description);
                    if (!mNativeDevice)
                    {
                        mError = "nvrhi::vulkan::createDevice failed.";
                        return EArdaInitializeResult::Failure;
                    }

                    mDevice = Configuration.mbEnableValidation
                        ? nvrhi::validation::createValidationLayer(mNativeDevice)
                        : nvrhi::DeviceHandle(mNativeDevice);
                    if (!mDevice)
                    {
                        mError = "Failed to create the NVRHI Vulkan device.";
                        return EArdaInitializeResult::Failure;
                    }
                }
                catch (const vk::SystemError& Exception)
                {
                    mError = std::string("Vulkan initialization failed: ") + Exception.what();
                    return EArdaInitializeResult::Unavailable;
                }
                catch (const std::exception& Exception)
                {
                    mError = std::string("Window-surface initialization failed: ") +
                        Exception.what();
                    return EArdaInitializeResult::Failure;
                }

                mError.clear();
                return EArdaInitializeResult::Success;
            }

            std::unique_ptr<IArdaSwapChain> CreateSwapChain(
                uint32_t Width,
                uint32_t Height) override
            {
                if (!mSurface)
                {
                    mError = "Vulkan presentation was not initialized with a window surface.";
                    return nullptr;
                }

                auto SwapChain = std::make_unique<FArdaVulkanSwapChain>(
                    mPhysicalDevice,
                    mVulkanDevice,
                    mGraphicsQueue,
                    mSurface,
                    mNativeDevice,
                    mDevice,
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
                if (mVulkanDevice)
                {
                    try
                    {
                        mVulkanDevice.waitIdle();
                    }
                    catch (const vk::SystemError&)
                    {
                    }
                }
            }

            nvrhi::DeviceHandle GetDevice() const noexcept override
            {
                return mDevice;
            }

            const std::string& GetError() const noexcept override
            {
                return mError;
            }

        private:
            bool SelectPhysicalDevice()
            {
                for (const auto Candidate : mInstance.enumeratePhysicalDevices())
                {
                    if (Candidate.getProperties().apiVersion < VK_API_VERSION_1_3)
                    {
                        continue;
                    }

                    vk::PhysicalDeviceVulkan13Features Features13;
                    vk::PhysicalDeviceVulkan12Features Features12;
                    Features12.setPNext(&Features13);
                    vk::PhysicalDeviceFeatures2 Features;
                    Features.setPNext(&Features12);
                    Candidate.getFeatures2(&Features);
                    if (!Features13.dynamicRendering ||
                        !Features13.synchronization2 ||
                        !Features12.timelineSemaphore)
                    {
                        continue;
                    }

                    if (mSurface)
                    {
                        bool bHasSwapChain = false;
                        for (const auto& Extension :
                             Candidate.enumerateDeviceExtensionProperties())
                        {
                            if (std::strcmp(
                                Extension.extensionName,
                                VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                            {
                                bHasSwapChain = true;
                                break;
                            }
                        }
                        if (!bHasSwapChain)
                        {
                            continue;
                        }
                    }

                    const auto QueueFamilies = Candidate.getQueueFamilyProperties();
                    for (uint32_t Index = 0; Index < QueueFamilies.size(); ++Index)
                    {
                        if (!(QueueFamilies[Index].queueFlags &
                              vk::QueueFlagBits::eGraphics))
                        {
                            continue;
                        }
                        if (mSurface && !Candidate.getSurfaceSupportKHR(Index, mSurface))
                        {
                            continue;
                        }

                        mPhysicalDevice = Candidate;
                        mGraphicsQueueFamily = Index;
                        return true;
                    }
                }

                return false;
            }

            std::string mError;
            vk::detail::DynamicLoader mLoader;
            std::vector<const char*> mInstanceExtensions;
            vk::Instance mInstance;
            vk::SurfaceKHR mSurface;
            vk::PhysicalDevice mPhysicalDevice;
            vk::Device mVulkanDevice;
            vk::Queue mGraphicsQueue;
            uint32_t mGraphicsQueueFamily = 0;
            nvrhi::vulkan::DeviceHandle mNativeDevice;
            nvrhi::DeviceHandle mDevice;
        };
    }

    std::unique_ptr<IArdaBackendDevice> CreateVulkanBackendDevice()
    {
        return std::make_unique<FArdaVulkanBackendDevice>();
    }
}
