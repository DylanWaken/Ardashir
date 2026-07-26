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
                : PhysicalDevice(PhysicalDevice)
                , VulkanDevice(VulkanDevice)
                , GraphicsQueue(GraphicsQueue)
                , Surface(Surface)
                , NativeDevice(std::move(NativeDevice))
                , Device(std::move(Device))
                , Width(Width)
                , Height(Height)
            {
            }

            ~FArdaVulkanSwapChain() override
            {
                WaitForIdle();
                ReleaseSwapChain();
                if (VulkanDevice)
                {
                    for (const auto Semaphore : ImageAvailable)
                    {
                        if (Semaphore)
                        {
                            VulkanDevice.destroySemaphore(Semaphore);
                        }
                    }
                    for (const auto Semaphore : RenderFinished)
                    {
                        if (Semaphore)
                        {
                            VulkanDevice.destroySemaphore(Semaphore);
                        }
                    }
                }
            }

            [[nodiscard]] bool Initialize()
            {
                return CreateFrameSyncObjects() && CreateSwapChain(Width, Height);
            }

            bool Resize(uint32_t NewWidth, uint32_t NewHeight) override
            {
                if (NewWidth == 0 || NewHeight == 0 ||
                    (NewWidth == Width && NewHeight == Height))
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
                if (!SwapChain)
                {
                    Error = "The Vulkan swap chain is not initialized.";
                    return false;
                }

                try
                {
                    const auto Result = VulkanDevice.acquireNextImageKHR(
                        SwapChain,
                        std::numeric_limits<uint64_t>::max(),
                        ImageAvailable[FrameIndex]);
                    if (Result.result != vk::Result::eSuccess &&
                        Result.result != vk::Result::eSuboptimalKHR)
                    {
                        Error = "vkAcquireNextImageKHR failed.";
                        return false;
                    }

                    ImageIndex = Result.value;
                    NativeDevice->queueWaitForSemaphore(
                        nvrhi::CommandQueue::Graphics,
                        ImageAvailable[FrameIndex],
                        0);
                    OutFramebuffer = Framebuffers[ImageIndex];
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
                    Error = std::string("Vulkan image acquisition failed: ") + Exception.what();
                    return false;
                }
            }

            void PrepareSubmit() override
            {
                if (NativeDevice)
                {
                    NativeDevice->queueSignalSemaphore(
                        nvrhi::CommandQueue::Graphics,
                        RenderFinished[FrameIndex],
                        0);
                }
            }

            bool Present() override
            {
                if (!SwapChain)
                {
                    Error = "The Vulkan swap chain is not initialized.";
                    return false;
                }

                try
                {
                    const vk::Semaphore WaitSemaphore = RenderFinished[FrameIndex];
                    vk::PresentInfoKHR Description;
                    Description.setWaitSemaphores(WaitSemaphore);
                    Description.setSwapchains(SwapChain);
                    Description.setImageIndices(ImageIndex);

                    const vk::Result Result = GraphicsQueue.presentKHR(Description);
                    if (Result != vk::Result::eSuccess &&
                        Result != vk::Result::eSuboptimalKHR)
                    {
                        Error = "vkQueuePresentKHR failed.";
                        return false;
                    }

                    FrameIndex = (FrameIndex + 1) % FramesInFlight;
                    return true;
                }
                catch (const vk::OutOfDateKHRError&)
                {
                    return RecreateSwapChain();
                }
                catch (const vk::SystemError& Exception)
                {
                    Error = std::string("Vulkan presentation failed: ") + Exception.what();
                    return false;
                }
            }

            void WaitForIdle() noexcept override
            {
                if (Device)
                {
                    Device->waitForIdle();
                }
                if (VulkanDevice)
                {
                    try
                    {
                        VulkanDevice.waitIdle();
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
            static constexpr uint32_t FramesInFlight = 2;

            bool CreateSwapChain(uint32_t RequestedWidth, uint32_t RequestedHeight)
            {
                try
                {
                    const auto Capabilities =
                        PhysicalDevice.getSurfaceCapabilitiesKHR(Surface);
                    const auto Formats = PhysicalDevice.getSurfaceFormatsKHR(Surface);
                    if (Formats.empty())
                    {
                        Error = "The Vulkan surface exposes no formats.";
                        return false;
                    }

                    const auto Format = SelectSurfaceFormat(Formats);
                    if (Format.format != vk::Format::eB8G8R8A8Unorm)
                    {
                        Error = "The Vulkan surface does not support BGRA8_UNORM.";
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
                    Description.setSurface(Surface);
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

                    SwapChain = VulkanDevice.createSwapchainKHR(Description);
                    const auto Images = VulkanDevice.getSwapchainImagesKHR(SwapChain);
                    Width = Extent.width;
                    Height = Extent.height;
                    Textures.resize(Images.size());
                    Framebuffers.resize(Images.size());

                    for (size_t Index = 0; Index < Images.size(); ++Index)
                    {
                        const auto TextureDescription = nvrhi::TextureDesc()
                            .setDimension(nvrhi::TextureDimension::Texture2D)
                            .setWidth(Width)
                            .setHeight(Height)
                            .setFormat(nvrhi::Format::BGRA8_UNORM)
                            .setIsRenderTarget(true)
                            .setDebugName("Vulkan swap-chain image")
                            .enableAutomaticStateTracking(nvrhi::ResourceStates::Present);

                        const auto NativeImage =
                            reinterpret_cast<uint64_t>(static_cast<VkImage>(Images[Index]));
                        Textures[Index] = Device->createHandleForNativeTexture(
                            nvrhi::ObjectTypes::VK_Image,
                            nvrhi::Object(NativeImage),
                            TextureDescription);
                        if (!Textures[Index])
                        {
                            Error = "NVRHI failed to wrap a Vulkan swap-chain image.";
                            ReleaseSwapChain();
                            return false;
                        }

                        Framebuffers[Index] = Device->createFramebuffer(
                            nvrhi::FramebufferDesc().addColorAttachment(Textures[Index]));
                        if (!Framebuffers[Index])
                        {
                            Error = "NVRHI failed to create a Vulkan framebuffer.";
                            ReleaseSwapChain();
                            return false;
                        }
                    }
                }
                catch (const vk::SystemError& Exception)
                {
                    Error = std::string("Vulkan swap-chain creation failed: ") + Exception.what();
                    ReleaseSwapChain();
                    return false;
                }

                Error.clear();
                return true;
            }

            void ReleaseSwapChain()
            {
                Framebuffers.clear();
                Textures.clear();
                if (Device)
                {
                    Device->runGarbageCollection();
                }
                if (SwapChain && VulkanDevice)
                {
                    VulkanDevice.destroySwapchainKHR(SwapChain);
                    SwapChain = nullptr;
                }
            }

            bool CreateFrameSyncObjects()
            {
                try
                {
                    const vk::SemaphoreCreateInfo Description;
                    for (uint32_t Index = 0; Index < FramesInFlight; ++Index)
                    {
                        ImageAvailable[Index] = VulkanDevice.createSemaphore(Description);
                        RenderFinished[Index] = VulkanDevice.createSemaphore(Description);
                    }
                }
                catch (const vk::SystemError& Exception)
                {
                    Error = std::string("Vulkan semaphore creation failed: ") + Exception.what();
                    return false;
                }

                return true;
            }

            bool RecreateSwapChain()
            {
                WaitForIdle();
                ReleaseSwapChain();
                return CreateSwapChain(Width, Height);
            }

            vk::PhysicalDevice PhysicalDevice;
            vk::Device VulkanDevice;
            vk::Queue GraphicsQueue;
            vk::SurfaceKHR Surface;
            nvrhi::vulkan::DeviceHandle NativeDevice;
            nvrhi::DeviceHandle Device;
            uint32_t Width = 0;
            uint32_t Height = 0;
            uint32_t ImageIndex = 0;
            uint32_t FrameIndex = 0;
            std::string Error;
            vk::SwapchainKHR SwapChain;
            std::vector<nvrhi::TextureHandle> Textures;
            std::vector<nvrhi::FramebufferHandle> Framebuffers;
            std::array<vk::Semaphore, FramesInFlight> ImageAvailable;
            std::array<vk::Semaphore, FramesInFlight> RenderFinished;
        };

        class FArdaVulkanBackendDevice final : public IArdaBackendDevice
        {
        public:
            ~FArdaVulkanBackendDevice() override
            {
                WaitForIdle();
                Device = nullptr;
                NativeDevice = nullptr;

                if (VulkanDevice)
                {
                    VulkanDevice.destroy();
                }
                if (Surface && Instance)
                {
                    Instance.destroySurfaceKHR(Surface);
                }
                if (Instance)
                {
                    Instance.destroy();
                }
            }

            EArdaInitializeResult Initialize(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface) override
            {
                try
                {
                    const auto GetInstanceProcAddress =
                        Loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
                    if (!GetInstanceProcAddress)
                    {
                        Error = "The Vulkan loader is not installed.";
                        return EArdaInitializeResult::Unavailable;
                    }
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(GetInstanceProcAddress);

                    if (WindowSurface)
                    {
                        InstanceExtensions = WindowSurface->GetVulkanInstanceExtensions();
                        if (InstanceExtensions.empty())
                        {
                            Error =
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
                    InstanceDescription.setPEnabledExtensionNames(InstanceExtensions);
                    Instance = vk::createInstance(InstanceDescription);
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(Instance);

                    if (WindowSurface)
                    {
                        std::string SurfaceError;
                        const nvrhi::Object SurfaceObject = WindowSurface->CreateVulkanSurface(
                            nvrhi::Object(static_cast<VkInstance>(Instance)),
                            SurfaceError);
                        const VkSurfaceKHR NativeSurface = DecodeVulkanSurface(SurfaceObject);
                        if (NativeSurface == VK_NULL_HANDLE)
                        {
                            Error = SurfaceError.empty()
                                ? "The window surface failed to create a Vulkan surface."
                                : SurfaceError;
                            return EArdaInitializeResult::Unavailable;
                        }
                        Surface = NativeSurface;
                    }

                    if (!SelectPhysicalDevice())
                    {
                        Error = Surface
                            ? "No Vulkan 1.3 device with required graphics and presentation "
                              "features is available."
                            : "No Vulkan 1.3 device with graphics, dynamic rendering, "
                              "synchronization2, and timeline semaphore support is available.";
                        return EArdaInitializeResult::Unavailable;
                    }

                    const float QueuePriority = 1.f;
                    vk::DeviceQueueCreateInfo QueueDescription;
                    QueueDescription.setQueueFamilyIndex(GraphicsQueueFamily);
                    QueueDescription.setQueueCount(1);
                    QueueDescription.setPQueuePriorities(&QueuePriority);

                    vk::PhysicalDeviceVulkan13Features Features13;
                    Features13.setDynamicRendering(true);
                    Features13.setSynchronization2(true);

                    vk::PhysicalDeviceVulkan12Features Features12;
                    Features12.setTimelineSemaphore(true);
                    Features12.setPNext(&Features13);

                    std::vector<const char*> DeviceExtensions;
                    if (Surface)
                    {
                        DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
                    }

                    vk::DeviceCreateInfo DeviceDescription;
                    DeviceDescription.setPNext(&Features12);
                    DeviceDescription.setQueueCreateInfos(QueueDescription);
                    DeviceDescription.setPEnabledExtensionNames(DeviceExtensions);
                    VulkanDevice = PhysicalDevice.createDevice(DeviceDescription);
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(VulkanDevice);
                    GraphicsQueue = VulkanDevice.getQueue(GraphicsQueueFamily, 0);

                    nvrhi::vulkan::DeviceDesc Description;
                    Description.errorCB = Configuration.messageCallback;
                    Description.instance = Instance;
                    Description.physicalDevice = PhysicalDevice;
                    Description.device = VulkanDevice;
                    Description.graphicsQueue = GraphicsQueue;
                    Description.graphicsQueueIndex =
                        static_cast<int>(GraphicsQueueFamily);
                    Description.instanceExtensions = InstanceExtensions.data();
                    Description.numInstanceExtensions =
                        static_cast<uint32_t>(InstanceExtensions.size());
                    Description.deviceExtensions = DeviceExtensions.data();
                    Description.numDeviceExtensions =
                        static_cast<uint32_t>(DeviceExtensions.size());
                    NativeDevice = nvrhi::vulkan::createDevice(Description);
                    if (!NativeDevice)
                    {
                        Error = "nvrhi::vulkan::createDevice failed.";
                        return EArdaInitializeResult::Failure;
                    }

                    Device = Configuration.enableValidation
                        ? nvrhi::validation::createValidationLayer(NativeDevice)
                        : nvrhi::DeviceHandle(NativeDevice);
                    if (!Device)
                    {
                        Error = "Failed to create the NVRHI Vulkan device.";
                        return EArdaInitializeResult::Failure;
                    }
                }
                catch (const vk::SystemError& Exception)
                {
                    Error = std::string("Vulkan initialization failed: ") + Exception.what();
                    return EArdaInitializeResult::Unavailable;
                }
                catch (const std::exception& Exception)
                {
                    Error = std::string("Window-surface initialization failed: ") +
                        Exception.what();
                    return EArdaInitializeResult::Failure;
                }

                Error.clear();
                return EArdaInitializeResult::Success;
            }

            std::unique_ptr<IArdaSwapChain> CreateSwapChain(
                uint32_t Width,
                uint32_t Height) override
            {
                if (!Surface)
                {
                    Error = "Vulkan presentation was not initialized with a window surface.";
                    return nullptr;
                }

                auto SwapChain = std::make_unique<FArdaVulkanSwapChain>(
                    PhysicalDevice,
                    VulkanDevice,
                    GraphicsQueue,
                    Surface,
                    NativeDevice,
                    Device,
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
                if (VulkanDevice)
                {
                    try
                    {
                        VulkanDevice.waitIdle();
                    }
                    catch (const vk::SystemError&)
                    {
                    }
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
            bool SelectPhysicalDevice()
            {
                for (const auto Candidate : Instance.enumeratePhysicalDevices())
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

                    if (Surface)
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
                        if (Surface && !Candidate.getSurfaceSupportKHR(Index, Surface))
                        {
                            continue;
                        }

                        PhysicalDevice = Candidate;
                        GraphicsQueueFamily = Index;
                        return true;
                    }
                }

                return false;
            }

            std::string Error;
            vk::detail::DynamicLoader Loader;
            std::vector<const char*> InstanceExtensions;
            vk::Instance Instance;
            vk::SurfaceKHR Surface;
            vk::PhysicalDevice PhysicalDevice;
            vk::Device VulkanDevice;
            vk::Queue GraphicsQueue;
            uint32_t GraphicsQueueFamily = 0;
            nvrhi::vulkan::DeviceHandle NativeDevice;
            nvrhi::DeviceHandle Device;
        };
    }

    std::unique_ptr<IArdaBackendDevice> CreateVulkanBackendDevice()
    {
        return std::make_unique<FArdaVulkanBackendDevice>();
    }
}
