#include "ArdaNvrhiPch.h"

#include "ArdaNvrhiBackendDevice.h"
#include "RHI/ArdaNvrhiDevice.h"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <cstring>
#include <EASTL/numeric_limits.h>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace arda::backend
{
    namespace
    {
        constexpr uint32_t InvalidQueueFamily = eastl::numeric_limits<uint32_t>::max();

        struct FArdaVulkanLifetime
        {
            FArdaNvrhiMessageCallback mMessageCallback;
            eastl::shared_ptr<vk::detail::DynamicLoader> mLoader;
            vk::Instance mInstance;
            vk::SurfaceKHR mSurface;
            vk::Device mDevice;

            ~FArdaVulkanLifetime()
            {
                if (mDevice) mDevice.destroy();
                if (mSurface && mInstance) mInstance.destroySurfaceKHR(mSurface);
                if (mInstance) mInstance.destroy();
            }
        };

        struct FArdaVulkanQueueSelection
        {
            uint32_t mGraphicsFamily = InvalidQueueFamily;
            uint32_t mGraphicsIndex = 0;
            uint32_t mComputeFamily = InvalidQueueFamily;
            uint32_t mComputeIndex = 0;
            uint32_t mCopyFamily = InvalidQueueFamily;
            uint32_t mCopyIndex = 0;
            eastl::vector<uint32_t> mRequestedQueueCounts;
        };

        template<typename QueueFamilyContainer>
        bool AllocateQueue(
            const QueueFamilyContainer& QueueFamilies,
            vk::QueueFlags RequiredFlags,
            vk::QueueFlags ExcludedFlags,
            FArdaVulkanQueueSelection& Selection,
            uint32_t& OutFamily,
            uint32_t& OutIndex)
        {
            for (uint32_t Family = 0; Family < QueueFamilies.size(); ++Family)
            {
                const vk::QueueFlags Flags = QueueFamilies[Family].queueFlags;
                if ((Flags & RequiredFlags) != RequiredFlags ||
                    static_cast<bool>(Flags & ExcludedFlags) ||
                    Selection.mRequestedQueueCounts[Family] >=
                        QueueFamilies[Family].queueCount)
                {
                    continue;
                }

                OutFamily = Family;
                OutIndex = Selection.mRequestedQueueCounts[Family]++;
                return true;
            }

            return false;
        }

        template<typename QueueFamilyContainer>
        FArdaVulkanQueueSelection SelectQueues(
            const QueueFamilyContainer& QueueFamilies,
            uint32_t GraphicsFamily)
        {
            FArdaVulkanQueueSelection Selection;
            Selection.mRequestedQueueCounts.resize(QueueFamilies.size());
            Selection.mGraphicsFamily = GraphicsFamily;
            Selection.mGraphicsIndex =
                Selection.mRequestedQueueCounts[GraphicsFamily]++;

            if (!AllocateQueue(
                    QueueFamilies,
                    vk::QueueFlagBits::eCompute,
                    vk::QueueFlagBits::eGraphics,
                    Selection,
                    Selection.mComputeFamily,
                    Selection.mComputeIndex))
            {
                AllocateQueue(
                    QueueFamilies,
                    vk::QueueFlagBits::eCompute,
                    {},
                    Selection,
                    Selection.mComputeFamily,
                    Selection.mComputeIndex);
            }

            if (!AllocateQueue(
                    QueueFamilies,
                    vk::QueueFlagBits::eTransfer,
                    vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute,
                    Selection,
                    Selection.mCopyFamily,
                    Selection.mCopyIndex) &&
                !AllocateQueue(
                    QueueFamilies,
                    vk::QueueFlagBits::eTransfer,
                    vk::QueueFlagBits::eGraphics,
                    Selection,
                    Selection.mCopyFamily,
                    Selection.mCopyIndex))
            {
                AllocateQueue(
                    QueueFamilies,
                    vk::QueueFlagBits::eTransfer,
                    {},
                    Selection,
                    Selection.mCopyFamily,
                    Selection.mCopyIndex);
            }

            return Selection;
        }

        vk::SurfaceFormatKHR SelectSurfaceFormat(
            const eastl::vector<vk::SurfaceFormatKHR>& Formats)
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

        eastl::string DescribeVulkanResult(vk::Result Result)
        {
            const std::string Description = vk::to_string(Result);
            return eastl::string(Description.data(), Description.size());
        }

        template<typename TValue>
        bool ExtractVulkanValue(
            const vk::ResultValue<TValue>& ResultValue,
            TValue& OutValue,
            eastl::string& Error,
            const char* Context)
        {
            if (ResultValue.result != vk::Result::eSuccess)
            {
                Error = eastl::string(Context) + ": " +
                    DescribeVulkanResult(ResultValue.result);
                return false;
            }

            OutValue = eastl::move(ResultValue.value);
            return true;
        }

        template<typename TValue, typename TAllocator>
        bool ExtractVulkanValue(
            const vk::ResultValue<std::vector<TValue, TAllocator>>& ResultValue,
            eastl::vector<TValue>& OutValue,
            eastl::string& Error,
            const char* Context)
        {
            if (ResultValue.result != vk::Result::eSuccess)
            {
                Error = eastl::string(Context) + ": " +
                    DescribeVulkanResult(ResultValue.result);
                return false;
            }

            OutValue.resize(ResultValue.value.size());
            for (size_t Index = 0; Index < ResultValue.value.size(); ++Index)
            {
                OutValue[Index] = ResultValue.value[Index];
            }
            return true;
        }

        VkSurfaceKHR DecodeVulkanSurface(FArdaNativeObject Surface)
        {
#if VK_USE_64_BIT_PTR_DEFINES
            return reinterpret_cast<VkSurfaceKHR>(Surface.mValue);
#else
            return static_cast<VkSurfaceKHR>(Surface.mValue);
#endif
        }

        class FArdaVulkanSwapChain final : public IArdaSwapChain
        {
        public:
            FArdaVulkanSwapChain(
                eastl::shared_ptr<void> BackendLifetime,
                vk::PhysicalDevice PhysicalDevice,
                vk::Device VulkanDevice,
                vk::Queue GraphicsQueue,
                vk::SurfaceKHR Surface,
                nvrhi::vulkan::DeviceHandle NativeDevice,
                nvrhi::DeviceHandle Device,
                rhi::FArdaRHIDeviceRef ArdaDevice,
                uint32_t Width,
                uint32_t Height)
                : mBackendLifetime(eastl::move(BackendLifetime))
                , mPhysicalDevice(PhysicalDevice)
                , mVulkanDevice(VulkanDevice)
                , mGraphicsQueue(GraphicsQueue)
                , mSurface(Surface)
                , mNativeDevice(eastl::move(NativeDevice))
                , mDevice(eastl::move(Device))
                , mArdaDevice(eastl::move(ArdaDevice))
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

            bool AcquireFrame(rhi::FArdaRHIFramebufferRef& OutFramebuffer) override
            {
                OutFramebuffer = nullptr;
                if (!mSwapChain)
                {
                    mError = "The Vulkan swap chain is not initialized.";
                    return false;
                }

                const auto AcquireResult = mVulkanDevice.acquireNextImageKHR(
                    mSwapChain,
                    eastl::numeric_limits<uint64_t>::max(),
                    mImageAvailable[mFrameIndex]);
                if (AcquireResult.result == vk::Result::eErrorOutOfDateKHR)
                {
                    if (!RecreateSwapChain())
                    {
                        return false;
                    }
                    return AcquireFrame(OutFramebuffer);
                }
                if (AcquireResult.result != vk::Result::eSuccess &&
                    AcquireResult.result != vk::Result::eSuboptimalKHR)
                {
                    mError = "Vulkan image acquisition failed: " +
                        DescribeVulkanResult(AcquireResult.result);
                    return false;
                }

                mImageIndex = AcquireResult.value;
                mNativeDevice->queueWaitForSemaphore(
                    nvrhi::CommandQueue::Graphics,
                    mImageAvailable[mFrameIndex],
                    0);
                OutFramebuffer = mArdaFramebuffers[mImageIndex];
                return static_cast<bool>(OutFramebuffer);
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

                const vk::Semaphore WaitSemaphore = mRenderFinished[mFrameIndex];
                vk::PresentInfoKHR Description;
                Description.setWaitSemaphores(WaitSemaphore);
                Description.setSwapchains(mSwapChain);
                Description.setImageIndices(mImageIndex);

                const vk::Result Result = mGraphicsQueue.presentKHR(Description);
                if (Result == vk::Result::eErrorOutOfDateKHR)
                {
                    return RecreateSwapChain();
                }
                if (Result != vk::Result::eSuccess &&
                    Result != vk::Result::eSuboptimalKHR)
                {
                    mError = "Vulkan presentation failed: " + DescribeVulkanResult(Result);
                    return false;
                }

                mFrameIndex = (mFrameIndex + 1) % mFramesInFlight;
                return true;
            }

            void WaitForIdle() noexcept override
            {
                if (mDevice)
                {
                    mDevice->waitForIdle();
                }
                if (mVulkanDevice)
                {
                    (void)mVulkanDevice.waitIdle();
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
            static constexpr uint32_t mFramesInFlight = 2;

            bool CreateSwapChain(uint32_t RequestedWidth, uint32_t RequestedHeight)
            {
                vk::SurfaceCapabilitiesKHR Capabilities;
                if (!ExtractVulkanValue(
                        mPhysicalDevice.getSurfaceCapabilitiesKHR(mSurface),
                        Capabilities,
                        mError,
                        "Failed to query Vulkan surface capabilities"))
                {
                    return false;
                }

                eastl::vector<vk::SurfaceFormatKHR> Formats;
                if (!ExtractVulkanValue(
                        mPhysicalDevice.getSurfaceFormatsKHR(mSurface),
                        Formats,
                        mError,
                        "Failed to query Vulkan surface formats"))
                {
                    return false;
                }
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
                    eastl::numeric_limits<uint32_t>::max())
                {
                    Extent = Capabilities.currentExtent;
                }
                else
                {
                    Extent.width = eastl::clamp(
                        RequestedWidth,
                        Capabilities.minImageExtent.width,
                        Capabilities.maxImageExtent.width);
                    Extent.height = eastl::clamp(
                        RequestedHeight,
                        Capabilities.minImageExtent.height,
                        Capabilities.maxImageExtent.height);
                }

                uint32_t ImageCount = Capabilities.minImageCount + 1;
                if (Capabilities.maxImageCount > 0)
                {
                    ImageCount = eastl::min(ImageCount, Capabilities.maxImageCount);
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

                const auto SwapChainResult =
                    mVulkanDevice.createSwapchainKHR(Description);
                if (SwapChainResult.result != vk::Result::eSuccess)
                {
                    mError = "Vulkan swap-chain creation failed: " +
                        DescribeVulkanResult(SwapChainResult.result);
                    ReleaseSwapChain();
                    return false;
                }

                mSwapChain = SwapChainResult.value;
                eastl::vector<vk::Image> Images;
                if (!ExtractVulkanValue(
                        mVulkanDevice.getSwapchainImagesKHR(mSwapChain),
                        Images,
                        mError,
                        "Failed to query Vulkan swap-chain images"))
                {
                    ReleaseSwapChain();
                    return false;
                }
                mWidth = Extent.width;
                mHeight = Extent.height;
                mArdaFramebuffers.resize(Images.size());

                for (size_t Index = 0; Index < Images.size(); ++Index)
                {
                    const auto NativeImage =
                        reinterpret_cast<uint64_t>(static_cast<VkImage>(Images[Index]));
                    rhi::FArdaRHINativeTextureImportDesc ImportDesc;
                    ImportDesc.mNativeObject =
                        static_cast<uintptr_t>(NativeImage);
                    ImportDesc.mNativeType =
                        rhi::EArdaRHINativeResourceType::VulkanImage;
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
                        "Vulkan swap-chain image";
                    auto ArdaTexture =
                        mArdaDevice->ImportNativeTexture(ImportDesc);
                    if (!ArdaTexture)
                    {
                        mError = ArdaTexture.mStatus.mMessage;
                        ReleaseSwapChain();
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
                        ReleaseSwapChain();
                        return false;
                    }
                    mArdaFramebuffers[Index] =
                        eastl::move(ArdaFramebuffer.mValue);
                }

                mError.clear();
                return true;
            }

            void ReleaseSwapChain()
            {
                mArdaFramebuffers.clear();
                if (mArdaDevice)
                {
                    mArdaDevice->TrimDescriptorCaches();
                }
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
                const vk::SemaphoreCreateInfo Description;
                for (uint32_t Index = 0; Index < mFramesInFlight; ++Index)
                {
                    const auto AvailableResult =
                        mVulkanDevice.createSemaphore(Description);
                    if (AvailableResult.result != vk::Result::eSuccess)
                    {
                        mError = "Vulkan semaphore creation failed: " +
                            DescribeVulkanResult(AvailableResult.result);
                        return false;
                    }

                    const auto FinishedResult =
                        mVulkanDevice.createSemaphore(Description);
                    if (FinishedResult.result != vk::Result::eSuccess)
                    {
                        mError = "Vulkan semaphore creation failed: " +
                            DescribeVulkanResult(FinishedResult.result);
                        return false;
                    }

                    mImageAvailable[Index] = AvailableResult.value;
                    mRenderFinished[Index] = FinishedResult.value;
                }

                return true;
            }

            bool RecreateSwapChain()
            {
                WaitForIdle();
                ReleaseSwapChain();
                return CreateSwapChain(mWidth, mHeight);
            }

            eastl::shared_ptr<void> mBackendLifetime;
            vk::PhysicalDevice mPhysicalDevice;
            vk::Device mVulkanDevice;
            vk::Queue mGraphicsQueue;
            vk::SurfaceKHR mSurface;
            nvrhi::vulkan::DeviceHandle mNativeDevice;
            nvrhi::DeviceHandle mDevice;
            rhi::FArdaRHIDeviceRef mArdaDevice;
            uint32_t mWidth = 0;
            uint32_t mHeight = 0;
            uint32_t mImageIndex = 0;
            uint32_t mFrameIndex = 0;
            eastl::string mError;
            vk::SwapchainKHR mSwapChain;
            eastl::vector<rhi::FArdaRHIFramebufferRef> mArdaFramebuffers;
            eastl::array<vk::Semaphore, mFramesInFlight> mImageAvailable;
            eastl::array<vk::Semaphore, mFramesInFlight> mRenderFinished;
        };

        class FArdaVulkanBackendDevice final : public IArdaBackendDevice
        {
        public:
            ~FArdaVulkanBackendDevice() override
            {
                WaitForIdle();
                mArdaDevice = nullptr;
                mDevice = nullptr;
                mNativeDevice = nullptr;

                if (mLifetime)
                {
                    mLifetime.reset();
                }
                else
                {
                    if (mVulkanDevice) mVulkanDevice.destroy();
                    if (mSurface && mInstance) mInstance.destroySurfaceKHR(mSurface);
                    if (mInstance) mInstance.destroy();
                }
            }

            EArdaInitializeResult Initialize(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface,
                const IArdaExternalDeviceProvider*) override
            {
                const auto GetInstanceProcAddress =
                    mLoader->getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
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
                const auto InstanceResult = vk::createInstance(InstanceDescription);
                if (InstanceResult.result != vk::Result::eSuccess)
                {
                    mError = "Failed to create the Vulkan instance: " +
                        DescribeVulkanResult(InstanceResult.result);
                    return EArdaInitializeResult::Unavailable;
                }
                mInstance = InstanceResult.value;
                VULKAN_HPP_DEFAULT_DISPATCHER.init(mInstance);

                if (WindowSurface)
                {
                    eastl::string SurfaceError;
                    const FArdaNativeObject SurfaceObject = WindowSurface->CreateVulkanSurface(
                        FArdaNativeObject(reinterpret_cast<uintptr_t>(static_cast<VkInstance>(mInstance))),
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

                const auto QueueFamilies = mPhysicalDevice.getQueueFamilyProperties();
                uint32_t MaximumQueueCount = 1;
                for (const uint32_t QueueCount : mQueueSelection.mRequestedQueueCounts)
                {
                    MaximumQueueCount = eastl::max(MaximumQueueCount, QueueCount);
                }
                const eastl::vector<float> QueuePriorities(MaximumQueueCount, 1.f);
                eastl::vector<vk::DeviceQueueCreateInfo> QueueDescriptions;
                for (uint32_t Family = 0; Family < QueueFamilies.size(); ++Family)
                {
                    const uint32_t QueueCount =
                        mQueueSelection.mRequestedQueueCounts[Family];
                    if (QueueCount == 0)
                    {
                        continue;
                    }

                    vk::DeviceQueueCreateInfo QueueDescription;
                    QueueDescription.setQueueFamilyIndex(Family);
                    QueueDescription.setQueueCount(QueueCount);
                    QueueDescription.setPQueuePriorities(QueuePriorities.data());
                    QueueDescriptions.push_back(QueueDescription);
                }

                vk::PhysicalDeviceVulkan13Features Features13;
                Features13.setDynamicRendering(true);
                Features13.setSynchronization2(true);

                vk::PhysicalDeviceVulkan12Features Features12;
                Features12.setTimelineSemaphore(true);
                Features12.setPNext(&Features13);

                eastl::vector<const char*> DeviceExtensions;
                if (mSurface)
                {
                    DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
                }

                vk::DeviceCreateInfo DeviceDescription;
                DeviceDescription.setPNext(&Features12);
                DeviceDescription.setQueueCreateInfos(QueueDescriptions);
                DeviceDescription.setPEnabledExtensionNames(DeviceExtensions);
                const auto DeviceResult = mPhysicalDevice.createDevice(DeviceDescription);
                if (DeviceResult.result != vk::Result::eSuccess)
                {
                    mError = "Failed to create the Vulkan device: " +
                        DescribeVulkanResult(DeviceResult.result);
                    return EArdaInitializeResult::Unavailable;
                }
                mVulkanDevice = DeviceResult.value;
                mLifetime = eastl::make_shared<FArdaVulkanLifetime>();
                mLifetime->mLoader = mLoader;
                mLifetime->mInstance = mInstance;
                mLifetime->mSurface = mSurface;
                mLifetime->mDevice = mVulkanDevice;
                VULKAN_HPP_DEFAULT_DISPATCHER.init(mVulkanDevice);
                mGraphicsQueue = mVulkanDevice.getQueue(
                    mQueueSelection.mGraphicsFamily,
                    mQueueSelection.mGraphicsIndex);
                if (mQueueSelection.mComputeFamily != InvalidQueueFamily)
                {
                    mComputeQueue = mVulkanDevice.getQueue(
                        mQueueSelection.mComputeFamily,
                        mQueueSelection.mComputeIndex);
                }
                if (mQueueSelection.mCopyFamily != InvalidQueueFamily)
                {
                    mCopyQueue = mVulkanDevice.getQueue(
                        mQueueSelection.mCopyFamily,
                        mQueueSelection.mCopyIndex);
                }

                nvrhi::vulkan::DeviceDesc Description;
                mLifetime->mMessageCallback.SetTarget(
                    Configuration.mMessageCallback);
                Description.errorCB = &mLifetime->mMessageCallback;
                Description.instance = mInstance;
                Description.physicalDevice = mPhysicalDevice;
                Description.device = mVulkanDevice;
                Description.graphicsQueue = mGraphicsQueue;
                Description.graphicsQueueIndex =
                    static_cast<int>(mQueueSelection.mGraphicsFamily);
                if (mComputeQueue)
                {
                    Description.computeQueue = mComputeQueue;
                    Description.computeQueueIndex =
                        static_cast<int>(mQueueSelection.mComputeFamily);
                }
                if (mCopyQueue)
                {
                    Description.transferQueue = mCopyQueue;
                    Description.transferQueueIndex =
                        static_cast<int>(mQueueSelection.mCopyFamily);
                }
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
                mArdaDevice = rhi::private_impl::CreateArdaNvrhiDevice(
                    mDevice, mLifetime, Configuration.mBackendName,
                    Configuration.mPipelineCacheDirectory,
                    Configuration.mMessageCallback);
                if (!mArdaDevice)
                {
                    mError = "Failed to create the opaque Arda Vulkan device.";
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
                if (!mSurface)
                {
                    mError = "Vulkan presentation was not initialized with a window surface.";
                    return nullptr;
                }

                auto SwapChain = eastl::make_unique<FArdaVulkanSwapChain>(
                    mLifetime,
                    mPhysicalDevice,
                    mVulkanDevice,
                    mGraphicsQueue,
                    mSurface,
                    mNativeDevice,
                    mDevice,
                    mArdaDevice,
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
                    (void)mVulkanDevice.waitIdle();
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
            bool SelectPhysicalDevice()
            {
                const auto DevicesResult = mInstance.enumeratePhysicalDevices();
                if (DevicesResult.result != vk::Result::eSuccess)
                {
                    return false;
                }

                for (const auto Candidate : DevicesResult.value)
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
                        const auto ExtensionsResult =
                            Candidate.enumerateDeviceExtensionProperties();
                        if (ExtensionsResult.result != vk::Result::eSuccess)
                        {
                            continue;
                        }

                        bool bHasSwapChain = false;
                        for (const auto& Extension : ExtensionsResult.value)
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
                        if (mSurface)
                        {
                            const auto SurfaceSupportResult =
                                Candidate.getSurfaceSupportKHR(Index, mSurface);
                            if (SurfaceSupportResult.result != vk::Result::eSuccess ||
                                !SurfaceSupportResult.value)
                            {
                                continue;
                            }
                        }

                        mPhysicalDevice = Candidate;
                        mQueueSelection = SelectQueues(QueueFamilies, Index);
                        return true;
                    }
                }

                return false;
            }

            eastl::string mError;
            eastl::shared_ptr<vk::detail::DynamicLoader> mLoader =
                eastl::make_shared<vk::detail::DynamicLoader>();
            eastl::vector<const char*> mInstanceExtensions;
            eastl::shared_ptr<FArdaVulkanLifetime> mLifetime;
            vk::Instance mInstance;
            vk::SurfaceKHR mSurface;
            vk::PhysicalDevice mPhysicalDevice;
            vk::Device mVulkanDevice;
            vk::Queue mGraphicsQueue;
            vk::Queue mComputeQueue;
            vk::Queue mCopyQueue;
            FArdaVulkanQueueSelection mQueueSelection;
            nvrhi::vulkan::DeviceHandle mNativeDevice;
            nvrhi::DeviceHandle mDevice;
            rhi::FArdaRHIDeviceRef mArdaDevice;
            FArdaQueueCapabilities mQueueCapabilities;
        };
    }

    eastl::unique_ptr<IArdaBackendDevice> CreateVulkanBackendDevice()
    {
        return eastl::make_unique<FArdaVulkanBackendDevice>();
    }
}
