#include "NVRHITestPch.h"

#include "VulkanBackend.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace arda::tests::nvrhi_test
{
    namespace
    {
        vk::SurfaceFormatKHR SelectSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats)
        {
            for (const auto& format : formats)
            {
                if (format.format == vk::Format::eB8G8R8A8Unorm &&
                    format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                {
                    return format;
                }
            }
            return formats.front();
        }

        vk::CompositeAlphaFlagBitsKHR SelectCompositeAlpha(vk::CompositeAlphaFlagsKHR supported)
        {
            constexpr vk::CompositeAlphaFlagBitsKHR candidates[] = {
                vk::CompositeAlphaFlagBitsKHR::eOpaque,
                vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
                vk::CompositeAlphaFlagBitsKHR::eInherit
            };

            for (const auto candidate : candidates)
            {
                if (supported & candidate)
                {
                    return candidate;
                }
            }
            return vk::CompositeAlphaFlagBitsKHR::eOpaque;
        }
    }

    VulkanBackend::~VulkanBackend()
    {
        WaitForIdle();
        ReleaseSwapChain();

        m_device = nullptr;
        m_nativeDevice = nullptr;

        if (m_vkDevice)
        {
            for (auto semaphore : m_imageAvailable)
            {
                if (semaphore)
                {
                    m_vkDevice.destroySemaphore(semaphore);
                }
            }
            for (auto semaphore : m_renderFinished)
            {
                if (semaphore)
                {
                    m_vkDevice.destroySemaphore(semaphore);
                }
            }
            m_vkDevice.destroy();
        }
        if (m_surface && m_instance)
        {
            m_instance.destroySurfaceKHR(m_surface);
        }
        if (m_instance)
        {
            m_instance.destroy();
        }
    }

    InitializeResult VulkanBackend::Initialize(
        GLFWwindow* window,
        uint32_t width,
        uint32_t height,
        nvrhi::IMessageCallback* messageCallback)
    {
        m_window = window;
        m_width = width;
        m_height = height;

        try
        {
            const auto getInstanceProcAddress =
                m_loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
            if (!getInstanceProcAddress)
            {
                m_error = "The Vulkan loader is not installed.";
                return InitializeResult::Unavailable;
            }
            VULKAN_HPP_DEFAULT_DISPATCHER.init(getInstanceProcAddress);

            uint32_t instanceExtensionCount = 0;
            const char** instanceExtensions = glfwGetRequiredInstanceExtensions(&instanceExtensionCount);
            if (!instanceExtensions || instanceExtensionCount == 0)
            {
                m_error = "GLFW did not provide the required Vulkan instance extensions.";
                return InitializeResult::Unavailable;
            }

            const vk::ApplicationInfo applicationInfo(
                "Ardashir NVRHITest",
                VK_MAKE_VERSION(1, 0, 0),
                "Ardashir",
                VK_MAKE_VERSION(1, 0, 0),
                VK_API_VERSION_1_3);

            vk::InstanceCreateInfo instanceInfo;
            instanceInfo.setPApplicationInfo(&applicationInfo);
            instanceInfo.setEnabledExtensionCount(instanceExtensionCount);
            instanceInfo.setPpEnabledExtensionNames(instanceExtensions);
            m_instance = vk::createInstance(instanceInfo);
            VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);

            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(m_instance, window, nullptr, &surface) != VK_SUCCESS)
            {
                m_error = "GLFW failed to create a Vulkan window surface.";
                return InitializeResult::Unavailable;
            }
            m_surface = surface;

            for (const auto physicalDevice : m_instance.enumeratePhysicalDevices())
            {
                const auto properties = physicalDevice.getProperties();
                if (properties.apiVersion < VK_API_VERSION_1_3)
                {
                    continue;
                }

                const auto queueFamilies = physicalDevice.getQueueFamilyProperties();
                for (uint32_t index = 0; index < queueFamilies.size(); ++index)
                {
                    if ((queueFamilies[index].queueFlags & vk::QueueFlagBits::eGraphics) &&
                        physicalDevice.getSurfaceSupportKHR(index, m_surface))
                    {
                        m_physicalDevice = physicalDevice;
                        m_queueFamily = index;
                        break;
                    }
                }

                if (m_physicalDevice)
                {
                    break;
                }
            }

            if (!m_physicalDevice)
            {
                m_error = "No Vulkan 1.3 device with graphics and presentation support is available.";
                return InitializeResult::Unavailable;
            }

            const float queuePriority = 1.f;
            vk::DeviceQueueCreateInfo queueInfo;
            queueInfo.setQueueFamilyIndex(m_queueFamily);
            queueInfo.setQueueCount(1);
            queueInfo.setPQueuePriorities(&queuePriority);

            vk::PhysicalDeviceVulkan13Features features13;
            features13.setDynamicRendering(true);
            features13.setSynchronization2(true);

            vk::PhysicalDeviceVulkan12Features features12;
            features12.setTimelineSemaphore(true);
            features12.setPNext(&features13);

            const char* deviceExtensions[] = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME
            };

            vk::DeviceCreateInfo deviceInfo;
            deviceInfo.setPNext(&features12);
            deviceInfo.setQueueCreateInfos(queueInfo);
            deviceInfo.setPEnabledExtensionNames(deviceExtensions);

            m_vkDevice = m_physicalDevice.createDevice(deviceInfo);
            VULKAN_HPP_DEFAULT_DISPATCHER.init(m_vkDevice);
            m_queue = m_vkDevice.getQueue(m_queueFamily, 0);

            nvrhi::vulkan::DeviceDesc nvrhiDesc;
            nvrhiDesc.errorCB = messageCallback;
            nvrhiDesc.instance = m_instance;
            nvrhiDesc.physicalDevice = m_physicalDevice;
            nvrhiDesc.device = m_vkDevice;
            nvrhiDesc.graphicsQueue = m_queue;
            nvrhiDesc.graphicsQueueIndex = static_cast<int>(m_queueFamily);
            nvrhiDesc.instanceExtensions = instanceExtensions;
            nvrhiDesc.numInstanceExtensions = instanceExtensionCount;
            nvrhiDesc.deviceExtensions = deviceExtensions;
            nvrhiDesc.numDeviceExtensions = std::size(deviceExtensions);

            m_nativeDevice = nvrhi::vulkan::createDevice(nvrhiDesc);
            if (!m_nativeDevice)
            {
                m_error = "nvrhi::vulkan::createDevice failed.";
                return InitializeResult::Failure;
            }

            m_device = nvrhi::validation::createValidationLayer(m_nativeDevice);
            if (!m_device || !CreateFrameSyncObjects() || !CreateSwapChain(width, height))
            {
                if (m_error.empty())
                {
                    m_error = "Failed to create NVRHI Vulkan resources.";
                }
                return InitializeResult::Failure;
            }
        }
        catch (const vk::SystemError& error)
        {
            m_error = std::string("Vulkan initialization failed: ") + error.what();
            return InitializeResult::Unavailable;
        }

        return InitializeResult::Success;
    }

    bool VulkanBackend::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0 || (width == m_width && height == m_height))
        {
            return true;
        }

        WaitForIdle();
        ReleaseSwapChain();
        return CreateSwapChain(width, height);
    }

    bool VulkanBackend::AcquireFrame(nvrhi::FramebufferHandle& framebuffer)
    {
        try
        {
            const auto result = m_vkDevice.acquireNextImageKHR(
                m_swapChain,
                std::numeric_limits<uint64_t>::max(),
                m_imageAvailable[m_frameIndex]);

            if (result.result != vk::Result::eSuccess && result.result != vk::Result::eSuboptimalKHR)
            {
                m_error = "vkAcquireNextImageKHR failed.";
                return false;
            }

            m_imageIndex = result.value;
            m_nativeDevice->queueWaitForSemaphore(
                nvrhi::CommandQueue::Graphics,
                m_imageAvailable[m_frameIndex],
                0);
            framebuffer = m_framebuffers[m_imageIndex];
            return framebuffer != nullptr;
        }
        catch (const vk::OutOfDateKHRError&)
        {
            WaitForIdle();
            ReleaseSwapChain();
            if (!CreateSwapChain(m_width, m_height))
            {
                return false;
            }
            return AcquireFrame(framebuffer);
        }
        catch (const vk::SystemError& error)
        {
            m_error = std::string("Vulkan image acquisition failed: ") + error.what();
            return false;
        }
    }

    void VulkanBackend::PrepareSubmit()
    {
        m_nativeDevice->queueSignalSemaphore(
            nvrhi::CommandQueue::Graphics,
            m_renderFinished[m_frameIndex],
            0);
    }

    bool VulkanBackend::Present()
    {
        try
        {
            const vk::Semaphore waitSemaphore = m_renderFinished[m_frameIndex];
            vk::PresentInfoKHR presentInfo;
            presentInfo.setWaitSemaphores(waitSemaphore);
            presentInfo.setSwapchains(m_swapChain);
            presentInfo.setImageIndices(m_imageIndex);

            const vk::Result result = m_queue.presentKHR(presentInfo);
            if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
            {
                m_error = "vkQueuePresentKHR failed.";
                return false;
            }

            m_frameIndex = (m_frameIndex + 1) % FramesInFlight;
            return true;
        }
        catch (const vk::OutOfDateKHRError&)
        {
            WaitForIdle();
            ReleaseSwapChain();
            return CreateSwapChain(m_width, m_height);
        }
        catch (const vk::SystemError& error)
        {
            m_error = std::string("Vulkan presentation failed: ") + error.what();
            return false;
        }
    }

    void VulkanBackend::WaitForIdle()
    {
        if (m_device)
        {
            m_device->waitForIdle();
        }
        if (m_vkDevice)
        {
            try
            {
                m_vkDevice.waitIdle();
            }
            catch (const vk::SystemError&)
            {
            }
        }
    }

    bool VulkanBackend::CreateSwapChain(uint32_t width, uint32_t height)
    {
        try
        {
            const auto capabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface);
            const auto formats = m_physicalDevice.getSurfaceFormatsKHR(m_surface);
            if (formats.empty())
            {
                m_error = "The Vulkan surface exposes no formats.";
                return false;
            }

            const auto format = SelectSurfaceFormat(formats);
            if (format.format != vk::Format::eB8G8R8A8Unorm)
            {
                m_error = "The Vulkan surface does not support BGRA8_UNORM.";
                return false;
            }

            vk::Extent2D extent;
            if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            {
                extent = capabilities.currentExtent;
            }
            else
            {
                extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
                extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            }

            uint32_t imageCount = capabilities.minImageCount + 1;
            if (capabilities.maxImageCount > 0)
            {
                imageCount = std::min(imageCount, capabilities.maxImageCount);
            }

            vk::SwapchainCreateInfoKHR swapChainInfo;
            swapChainInfo.setSurface(m_surface);
            swapChainInfo.setMinImageCount(imageCount);
            swapChainInfo.setImageFormat(format.format);
            swapChainInfo.setImageColorSpace(format.colorSpace);
            swapChainInfo.setImageExtent(extent);
            swapChainInfo.setImageArrayLayers(1);
            swapChainInfo.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment);
            swapChainInfo.setImageSharingMode(vk::SharingMode::eExclusive);
            swapChainInfo.setPreTransform(capabilities.currentTransform);
            swapChainInfo.setCompositeAlpha(SelectCompositeAlpha(capabilities.supportedCompositeAlpha));
            swapChainInfo.setPresentMode(vk::PresentModeKHR::eFifo);
            swapChainInfo.setClipped(true);

            m_swapChain = m_vkDevice.createSwapchainKHR(swapChainInfo);
            const auto images = m_vkDevice.getSwapchainImagesKHR(m_swapChain);

            m_width = extent.width;
            m_height = extent.height;
            m_textures.resize(images.size());
            m_framebuffers.resize(images.size());

            for (size_t index = 0; index < images.size(); ++index)
            {
                const auto textureDesc = nvrhi::TextureDesc()
                    .setDimension(nvrhi::TextureDimension::Texture2D)
                    .setWidth(m_width)
                    .setHeight(m_height)
                    .setFormat(nvrhi::Format::BGRA8_UNORM)
                    .setIsRenderTarget(true)
                    .setDebugName("Vulkan swap-chain image")
                    .enableAutomaticStateTracking(nvrhi::ResourceStates::Present);

                const auto nativeImage = reinterpret_cast<uint64_t>(static_cast<VkImage>(images[index]));
                m_textures[index] = m_device->createHandleForNativeTexture(
                    nvrhi::ObjectTypes::VK_Image,
                    nvrhi::Object(nativeImage),
                    textureDesc);
                if (!m_textures[index])
                {
                    m_error = "NVRHI failed to wrap a Vulkan swap-chain image.";
                    return false;
                }

                m_framebuffers[index] = m_device->createFramebuffer(
                    nvrhi::FramebufferDesc().addColorAttachment(m_textures[index]));
                if (!m_framebuffers[index])
                {
                    m_error = "NVRHI failed to create a Vulkan framebuffer.";
                    return false;
                }
            }
        }
        catch (const vk::SystemError& error)
        {
            m_error = std::string("Vulkan swap-chain creation failed: ") + error.what();
            return false;
        }

        return true;
    }

    void VulkanBackend::ReleaseSwapChain()
    {
        m_framebuffers.clear();
        m_textures.clear();
        if (m_device)
        {
            m_device->runGarbageCollection();
        }
        if (m_swapChain && m_vkDevice)
        {
            m_vkDevice.destroySwapchainKHR(m_swapChain);
            m_swapChain = nullptr;
        }
    }

    bool VulkanBackend::CreateFrameSyncObjects()
    {
        try
        {
            const vk::SemaphoreCreateInfo info;
            for (uint32_t index = 0; index < FramesInFlight; ++index)
            {
                m_imageAvailable[index] = m_vkDevice.createSemaphore(info);
                m_renderFinished[index] = m_vkDevice.createSemaphore(info);
            }
        }
        catch (const vk::SystemError& error)
        {
            m_error = std::string("Vulkan semaphore creation failed: ") + error.what();
            return false;
        }

        return true;
    }
}
