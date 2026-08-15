#include "ArdaBackendPch.h"

#include "ArdaBackendDevice.h"
#include "RHIWrappers/ArdaNvrhiDevice.h"

namespace arda::backend
{
    namespace
    {
        struct FResourceProviderEntry
        {
            eastl::string mName;
            IArdaExternalResourceProvider* mProvider = nullptr;
        };

        struct FResourceProviderRegistry
        {
            std::mutex mMutex;
            eastl::vector<FResourceProviderEntry> mEntries;
        };

        FResourceProviderRegistry& GetResourceProviderRegistry()
        {
            static FResourceProviderRegistry Registry;
            return Registry;
        }

        struct FExternalDeviceLifetime
        {
            FArdaNvrhiMessageCallback mMessageCallback;
            eastl::shared_ptr<void> mProviderToken;
#if defined(_WIN32)
            FArdaExternalD3D12DeviceDesc mD3D12Desc;
#endif
            eastl::shared_ptr<vk::detail::DynamicLoader> mVulkanLoader;
            FArdaExternalVulkanDeviceDesc mVulkanDesc;
        };

#if defined(_WIN32)
        eastl::string DescribeHResult(HRESULT Result)
        {
            char Buffer[16] = {};
            std::snprintf(
                Buffer, sizeof(Buffer), "0x%08lX",
                static_cast<unsigned long>(Result));
            return Buffer;
        }

        bool GetComIdentity(
            IUnknown* Object,
            Microsoft::WRL::ComPtr<IUnknown>& OutIdentity,
            eastl::string& Error,
            const char* ObjectName)
        {
            if (!Object)
            {
                Error = eastl::string("External D3D12 ") + ObjectName +
                    " is null.";
                return false;
            }

            const HRESULT Result = Object->QueryInterface(
                IID_PPV_ARGS(OutIdentity.ReleaseAndGetAddressOf()));
            if (FAILED(Result))
            {
                Error = eastl::string("External D3D12 ") + ObjectName +
                    " does not expose COM identity (IUnknown): " +
                    DescribeHResult(Result) + ".";
                return false;
            }
            return true;
        }

        bool ValidateD3D12Queue(
            ID3D12CommandQueue* Queue,
            IUnknown* DeviceIdentity,
            const D3D12_COMMAND_LIST_TYPE* AllowedTypes,
            size_t AllowedTypeCount,
            eastl::string& Error,
            const char* QueueName)
        {
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> ValidatedQueue;
            const HRESULT InterfaceResult =
                reinterpret_cast<IUnknown*>(Queue)->QueryInterface(
                    IID_PPV_ARGS(ValidatedQueue.ReleaseAndGetAddressOf()));
            if (FAILED(InterfaceResult) || !ValidatedQueue)
            {
                Error = eastl::string("External D3D12 ") + QueueName +
                    " does not expose ID3D12CommandQueue: " +
                    DescribeHResult(InterfaceResult) + ".";
                return false;
            }

            Microsoft::WRL::ComPtr<IUnknown> QueueIdentity;
            if (!GetComIdentity(
                    ValidatedQueue.Get(), QueueIdentity, Error, QueueName))
                return false;

            const D3D12_COMMAND_LIST_TYPE QueueType =
                ValidatedQueue->GetDesc().Type;
            bool bAllowedType = false;
            for (size_t Index = 0; Index < AllowedTypeCount; ++Index)
                bAllowedType |= QueueType == AllowedTypes[Index];
            if (!bAllowedType)
            {
                Error = eastl::string("External D3D12 ") + QueueName +
                    " has an incompatible command-list type.";
                return false;
            }

            Microsoft::WRL::ComPtr<ID3D12Device> QueueDevice;
            const HRESULT DeviceResult = ValidatedQueue->GetDevice(
                IID_PPV_ARGS(QueueDevice.ReleaseAndGetAddressOf()));
            if (FAILED(DeviceResult) || !QueueDevice)
            {
                Error = eastl::string("External D3D12 ") + QueueName +
                    " GetDevice failed: " + DescribeHResult(DeviceResult) + ".";
                return false;
            }

            Microsoft::WRL::ComPtr<IUnknown> QueueDeviceIdentity;
            if (!GetComIdentity(
                    QueueDevice.Get(), QueueDeviceIdentity, Error,
                    "queue device"))
            {
                return false;
            }
            if (QueueDeviceIdentity.Get() != DeviceIdentity)
            {
                Error = eastl::string("External D3D12 ") + QueueName +
                    " belongs to a different device.";
                return false;
            }
            return true;
        }
#endif

        bool ValidateVulkanQueue(
            vk::Device Device,
            const eastl::vector<vk::QueueFamilyProperties>& QueueFamilies,
            const FArdaExternalVulkanQueueDesc& Queue,
            vk::QueueFlags RequiredFlags,
            eastl::string& Error,
            const char* QueueName)
        {
            if (Queue.mFamilyIndex >= QueueFamilies.size())
            {
                Error = eastl::string("External Vulkan ") + QueueName +
                    " family index is out of bounds.";
                return false;
            }
            const auto& Family = QueueFamilies[Queue.mFamilyIndex];
            if (Queue.mQueueIndex >= Family.queueCount)
            {
                Error = eastl::string("External Vulkan ") + QueueName +
                    " queue index is out of bounds.";
                return false;
            }
            if (!(Family.queueFlags & RequiredFlags))
            {
                Error = eastl::string("External Vulkan ") + QueueName +
                    " queue family lacks the required capability.";
                return false;
            }

            const vk::Queue SuppliedQueue(Queue.mQueue.As<VkQueue>());
            const vk::Queue DeviceQueue =
                Device.getQueue(Queue.mFamilyIndex, Queue.mQueueIndex);
            if (!DeviceQueue || DeviceQueue != SuppliedQueue)
            {
                Error = eastl::string("External Vulkan ") + QueueName +
                    " does not match device.getQueue(family, index).";
                return false;
            }
            return true;
        }

        class FArdaExternalBackendDevice final : public IArdaBackendDevice
        {
        public:
            EArdaInitializeResult Initialize(
                const FArdaBackendConfiguration&,
                IArdaWindowSurface*) override
            {
                mError = "An external device provider is required.";
                return EArdaInitializeResult::Failure;
            }

            EArdaInitializeResult Initialize(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface,
                const IArdaExternalDeviceProvider* ExternalProvider) override
            {
                if (!ExternalProvider)
                {
                    mError = "An external device provider is required.";
                    return EArdaInitializeResult::Failure;
                }
                if (WindowSurface)
                {
                    mError =
                        "Presentation with an external device is not supported by this backend wrapper.";
                    return EArdaInitializeResult::Failure;
                }

                mLifetime = eastl::make_shared<FExternalDeviceLifetime>();
                mLifetime->mMessageCallback.SetTarget(Configuration.mMessageCallback);
                mLifetime->mProviderToken = ExternalProvider->GetLifetimeToken();

                switch (Configuration.mBackend)
                {
                case EArdaBackendType::D3D12:
#if defined(_WIN32)
                    if (!InitializeD3D12(*ExternalProvider))
                        return EArdaInitializeResult::Failure;
                    break;
#else
                    mError = "D3D12 is only supported on Windows.";
                    return EArdaInitializeResult::Unavailable;
#endif
                case EArdaBackendType::Vulkan:
                {
                    const EArdaInitializeResult Result =
                        InitializeVulkan(*ExternalProvider);
                    if (Result != EArdaInitializeResult::Success)
                        return Result;
                    break;
                }
                }

                mDevice = Configuration.mbEnableValidation
                    ? nvrhi::validation::createValidationLayer(mNativeDevice)
                    : nvrhi::DeviceHandle(mNativeDevice);
                if (!mDevice)
                {
                    mError = "Failed to create the validated NVRHI external device.";
                    return EArdaInitializeResult::Failure;
                }

                mArdaDevice = rhi::private_impl::CreateArdaNvrhiDevice(
                    mDevice, mLifetime);
                if (!mArdaDevice)
                {
                    mError = "Failed to create the Arda facade for the external device.";
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
                uint32_t, uint32_t) override
            {
                mError =
                    "Presentation with an external device is not supported by this backend wrapper.";
                return {};
            }

            void WaitForIdle() noexcept override
            {
                if (mDevice)
                    mDevice->waitForIdle();
            }

            rhi::FArdaRHIDeviceRef GetDevice() const noexcept override
            {
                return mArdaDevice;
            }

            FArdaQueueCapabilities GetQueueCapabilities() const noexcept override
            {
                return mQueueCapabilities;
            }

            const eastl::string& GetError() const noexcept override { return mError; }

        private:
#if defined(_WIN32)
            bool InitializeD3D12(const IArdaExternalDeviceProvider& Provider)
            {
                auto& External = mLifetime->mD3D12Desc;
                if (!Provider.GetD3D12DeviceDesc(External))
                {
                    mError = "The external provider did not supply a D3D12 descriptor.";
                    return false;
                }
                if (!External.mDevice || !External.mGraphicsQueue)
                {
                    mError =
                        "External D3D12 initialization requires a device and graphics queue.";
                    return false;
                }
                if (External.mRenderTargetViewHeapSize == 0)
                {
                    mError =
                        "External D3D12 render-target-view heap capacity must be nonzero.";
                    return false;
                }
                if (External.mDepthStencilViewHeapSize == 0)
                {
                    mError =
                        "External D3D12 depth-stencil-view heap capacity must be nonzero.";
                    return false;
                }
                if (External.mShaderResourceViewHeapSize == 0)
                {
                    mError =
                        "External D3D12 shader-resource-view heap capacity must be nonzero.";
                    return false;
                }
                if (External.mSamplerHeapSize == 0)
                {
                    mError =
                        "External D3D12 sampler heap capacity must be nonzero.";
                    return false;
                }

                Microsoft::WRL::ComPtr<ID3D12Device> ValidatedDevice;
                const HRESULT DeviceInterfaceResult =
                    External.mDevice.As<IUnknown*>()->QueryInterface(
                        IID_PPV_ARGS(ValidatedDevice.ReleaseAndGetAddressOf()));
                if (FAILED(DeviceInterfaceResult) || !ValidatedDevice)
                {
                    mError =
                        "External D3D12 device does not expose ID3D12Device: " +
                        DescribeHResult(DeviceInterfaceResult) + ".";
                    return false;
                }

                ID3D12Device* Device = ValidatedDevice.Get();
                Microsoft::WRL::ComPtr<IUnknown> DeviceIdentity;
                if (!GetComIdentity(Device, DeviceIdentity, mError, "device"))
                    return false;

                constexpr D3D12_COMMAND_LIST_TYPE GraphicsTypes[] = {
                    D3D12_COMMAND_LIST_TYPE_DIRECT
                };
                constexpr D3D12_COMMAND_LIST_TYPE ComputeTypes[] = {
                    D3D12_COMMAND_LIST_TYPE_COMPUTE,
                    D3D12_COMMAND_LIST_TYPE_DIRECT
                };
                constexpr D3D12_COMMAND_LIST_TYPE CopyTypes[] = {
                    D3D12_COMMAND_LIST_TYPE_COPY,
                    D3D12_COMMAND_LIST_TYPE_COMPUTE,
                    D3D12_COMMAND_LIST_TYPE_DIRECT
                };
                if (!ValidateD3D12Queue(
                        External.mGraphicsQueue.As<ID3D12CommandQueue*>(),
                        DeviceIdentity.Get(), GraphicsTypes,
                        sizeof(GraphicsTypes) / sizeof(GraphicsTypes[0]),
                        mError, "graphics queue"))
                {
                    return false;
                }
                if (External.mComputeQueue &&
                    !ValidateD3D12Queue(
                        External.mComputeQueue.As<ID3D12CommandQueue*>(),
                        DeviceIdentity.Get(), ComputeTypes,
                        sizeof(ComputeTypes) / sizeof(ComputeTypes[0]),
                        mError, "compute queue"))
                {
                    return false;
                }
                if (External.mCopyQueue &&
                    !ValidateD3D12Queue(
                        External.mCopyQueue.As<ID3D12CommandQueue*>(),
                        DeviceIdentity.Get(), CopyTypes,
                        sizeof(CopyTypes) / sizeof(CopyTypes[0]),
                        mError, "copy queue"))
                {
                    return false;
                }

                nvrhi::d3d12::DeviceDesc Description;
                Description.errorCB = &mLifetime->mMessageCallback;
                Description.pDevice = Device;
                Description.pGraphicsCommandQueue =
                    External.mGraphicsQueue.As<ID3D12CommandQueue*>();
                Description.pComputeCommandQueue =
                    External.mComputeQueue.As<ID3D12CommandQueue*>();
                Description.pCopyCommandQueue =
                    External.mCopyQueue.As<ID3D12CommandQueue*>();
                Description.renderTargetViewHeapSize =
                    External.mRenderTargetViewHeapSize;
                Description.depthStencilViewHeapSize =
                    External.mDepthStencilViewHeapSize;
                Description.shaderResourceViewHeapSize =
                    External.mShaderResourceViewHeapSize;
                Description.samplerHeapSize = External.mSamplerHeapSize;
                Description.maxTimerQueries = External.mMaxTimerQueries;
                Description.enableHeapDirectlyIndexed =
                    External.mbEnableHeapDirectlyIndexed;
                Description.aftermathEnabled = External.mbAftermathEnabled;
                Description.logBufferLifetime = External.mbLogBufferLifetime;
                Description.enableRayTracingValidation =
                    External.mbEnableRayTracingValidation;
                Description.enableEnhancedBarriers =
                    External.mbEnableEnhancedBarriers;
                mNativeDevice = nvrhi::d3d12::createDevice(Description);
                if (!mNativeDevice)
                {
                    mError = "NVRHI failed to wrap the external D3D12 device.";
                    return false;
                }
                return true;
            }
#endif

            EArdaInitializeResult InitializeVulkan(
                const IArdaExternalDeviceProvider& Provider)
            {
                try
                {
                    return InitializeVulkanUnchecked(Provider);
                }
                catch (...)
                {
                    mError =
                        "External Vulkan initialization raised an unexpected C++ exception.";
                    return EArdaInitializeResult::Failure;
                }
            }

            EArdaInitializeResult InitializeVulkanUnchecked(
                const IArdaExternalDeviceProvider& Provider)
            {
                auto& External = mLifetime->mVulkanDesc;
                if (!Provider.GetVulkanDeviceDesc(External))
                {
                    mError = "The external provider did not supply a Vulkan descriptor.";
                    return EArdaInitializeResult::Failure;
                }
                if (!External.mInstance || !External.mPhysicalDevice ||
                    !External.mDevice || !External.mGraphicsQueue.mQueue)
                {
                    mError =
                        "External Vulkan initialization requires instance, physical device, device, and graphics queue.";
                    return EArdaInitializeResult::Failure;
                }

                if (External.mVulkanLibraryName.empty())
                {
                    mLifetime->mVulkanLoader =
                        eastl::make_shared<vk::detail::DynamicLoader>();
                }
                else
                {
                    mLifetime->mVulkanLoader =
                        eastl::make_shared<vk::detail::DynamicLoader>(
                            External.mVulkanLibraryName.c_str());
                }
                const PFN_vkGetInstanceProcAddr GetInstanceProcAddress =
                    mLifetime->mVulkanLoader
                    ? mLifetime->mVulkanLoader->getProcAddress<
                        PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr")
                    : nullptr;
                if (!GetInstanceProcAddress)
                {
                    mError = "The Vulkan loader is unavailable for the external device.";
                    return EArdaInitializeResult::Unavailable;
                }

                VULKAN_HPP_DEFAULT_DISPATCHER.init(GetInstanceProcAddress);
                const vk::Instance Instance(External.mInstance.As<VkInstance>());
                const vk::PhysicalDevice PhysicalDevice(
                    External.mPhysicalDevice.As<VkPhysicalDevice>());
                const vk::Device Device(External.mDevice.As<VkDevice>());
                VULKAN_HPP_DEFAULT_DISPATCHER.init(Instance);
                if (!VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceProperties ||
                    !VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceFeatures2 ||
                    !VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceQueueFamilyProperties ||
                    !VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr)
                {
                    mError =
                        "The external Vulkan instance does not expose required Vulkan 1.3 entry points.";
                    return EArdaInitializeResult::Unavailable;
                }

                const vk::PhysicalDeviceProperties Properties =
                    PhysicalDevice.getProperties();
                if (Properties.apiVersion < VK_API_VERSION_1_3)
                {
                    mError =
                        "External Vulkan physical device API version is below Vulkan 1.3.";
                    return EArdaInitializeResult::Unavailable;
                }

                vk::PhysicalDeviceVulkan13Features Features13;
                vk::PhysicalDeviceVulkan12Features Features12;
                Features12.setPNext(&Features13);
                vk::PhysicalDeviceFeatures2 Features;
                Features.setPNext(&Features12);
                PhysicalDevice.getFeatures2(&Features);
                if (!Features13.dynamicRendering)
                {
                    mError =
                        "External Vulkan physical device does not support dynamicRendering.";
                    return EArdaInitializeResult::Unavailable;
                }
                if (!Features13.synchronization2)
                {
                    mError =
                        "External Vulkan physical device does not support synchronization2.";
                    return EArdaInitializeResult::Unavailable;
                }
                if (!Features12.timelineSemaphore)
                {
                    mError =
                        "External Vulkan physical device does not support timelineSemaphore.";
                    return EArdaInitializeResult::Unavailable;
                }
                if (External.mbBufferDeviceAddressSupported &&
                    !Features12.bufferDeviceAddress)
                {
                    mError =
                        "External Vulkan descriptor claims bufferDeviceAddress, but the physical device does not support it.";
                    return EArdaInitializeResult::Unavailable;
                }

                VULKAN_HPP_DEFAULT_DISPATCHER.init(Device);
                if (!VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceQueue)
                {
                    mError =
                        "The external Vulkan device does not expose vkGetDeviceQueue.";
                    return EArdaInitializeResult::Unavailable;
                }

                const auto NativeQueueFamilies =
                    PhysicalDevice.getQueueFamilyProperties();
                eastl::vector<vk::QueueFamilyProperties> QueueFamilies;
                QueueFamilies.reserve(NativeQueueFamilies.size());
                for (const auto& QueueFamily : NativeQueueFamilies)
                    QueueFamilies.push_back(QueueFamily);
                if (!ValidateVulkanQueue(
                        Device, QueueFamilies, External.mGraphicsQueue,
                        vk::QueueFlagBits::eGraphics, mError, "graphics queue"))
                {
                    return EArdaInitializeResult::Failure;
                }
                if (External.mComputeQueue.mQueue &&
                    !ValidateVulkanQueue(
                        Device, QueueFamilies, External.mComputeQueue,
                        vk::QueueFlagBits::eCompute, mError, "compute queue"))
                {
                    return EArdaInitializeResult::Failure;
                }
                if (External.mCopyQueue.mQueue &&
                    !ValidateVulkanQueue(
                        Device, QueueFamilies, External.mCopyQueue,
                        vk::QueueFlagBits::eTransfer |
                            vk::QueueFlagBits::eCompute |
                            vk::QueueFlagBits::eGraphics,
                        mError, "copy queue"))
                {
                    return EArdaInitializeResult::Failure;
                }

                eastl::vector<const char*> InstanceExtensions;
                eastl::vector<const char*> DeviceExtensions;
                InstanceExtensions.reserve(External.mInstanceExtensions.size());
                DeviceExtensions.reserve(External.mDeviceExtensions.size());
                for (const auto& Extension : External.mInstanceExtensions)
                    InstanceExtensions.push_back(Extension.c_str());
                for (const auto& Extension : External.mDeviceExtensions)
                    DeviceExtensions.push_back(Extension.c_str());

                nvrhi::vulkan::DeviceDesc Description;
                Description.errorCB = &mLifetime->mMessageCallback;
                Description.instance = Instance;
                Description.physicalDevice = PhysicalDevice;
                Description.device = Device;
                Description.graphicsQueue =
                    External.mGraphicsQueue.mQueue.As<VkQueue>();
                Description.graphicsQueueIndex =
                    static_cast<int>(External.mGraphicsQueue.mFamilyIndex);
                if (External.mComputeQueue.mQueue)
                {
                    Description.computeQueue =
                        External.mComputeQueue.mQueue.As<VkQueue>();
                    Description.computeQueueIndex =
                        static_cast<int>(External.mComputeQueue.mFamilyIndex);
                }
                if (External.mCopyQueue.mQueue)
                {
                    Description.transferQueue =
                        External.mCopyQueue.mQueue.As<VkQueue>();
                    Description.transferQueueIndex =
                        static_cast<int>(External.mCopyQueue.mFamilyIndex);
                }
                Description.allocationCallbacks =
                    External.mAllocationCallbacks.As<VkAllocationCallbacks*>();
                Description.instanceExtensions = InstanceExtensions.data();
                Description.numInstanceExtensions = InstanceExtensions.size();
                Description.deviceExtensions = DeviceExtensions.data();
                Description.numDeviceExtensions = DeviceExtensions.size();
                Description.maxTimerQueries = External.mMaxTimerQueries;
                Description.bufferDeviceAddressSupported =
                    External.mbBufferDeviceAddressSupported;
                Description.aftermathEnabled = External.mbAftermathEnabled;
                Description.logBufferLifetime = External.mbLogBufferLifetime;
                Description.vulkanLibraryName =
                    External.mVulkanLibraryName.c_str();
                mNativeDevice = nvrhi::vulkan::createDevice(Description);
                if (!mNativeDevice)
                {
                    mError = "NVRHI failed to wrap the external Vulkan device.";
                    return EArdaInitializeResult::Failure;
                }
                return EArdaInitializeResult::Success;
            }

            eastl::shared_ptr<FExternalDeviceLifetime> mLifetime;
            nvrhi::DeviceHandle mNativeDevice;
            nvrhi::DeviceHandle mDevice;
            rhi::FArdaRHIDeviceRef mArdaDevice;
            FArdaQueueCapabilities mQueueCapabilities;
            eastl::string mError;
        };

        template <typename Ref, typename Desc, typename Resolver, typename Importer>
        rhi::TArdaRHIResult<Ref> ImportExternalResource(
            const char* ProviderName,
            uint64_t Id,
            Resolver Resolve,
            Importer Import)
        {
            if (!ProviderName || !ProviderName[0])
            {
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidArgument,
                    "External resource provider name must be non-empty.") };
            }

            auto& Registry = GetResourceProviderRegistry();
            std::lock_guard<std::mutex> Lock(Registry.mMutex);
            IArdaExternalResourceProvider* Provider = nullptr;
            for (const auto& Entry : Registry.mEntries)
            {
                if (Entry.mName == ProviderName)
                {
                    Provider = Entry.mProvider;
                    break;
                }
            }
            if (!Provider)
            {
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidArgument,
                    "External resource provider is not registered.") };
            }
            if (!IsBackendInitialized())
            {
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidState,
                    "External resources require an initialized backend.") };
            }
            if (Provider->GetBackendType() != GetDeviceContext().mBackend)
            {
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::WrongDevice,
                    "External resource provider backend does not match the active device.") };
            }

            Desc Description;
            rhi::FArdaRHIStatus Status = (Provider->*Resolve)(Id, Description);
            if (!Status)
                return { {}, std::move(Status) };
            rhi::FArdaRHIDeviceRef Device = GetDevice();
            if (!Device)
            {
                return { {}, rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidState,
                    "The active backend has no RHI device.") };
            }
            return (Device.Get()->*Import)(Description);
        }
    }

    eastl::unique_ptr<IArdaBackendDevice> CreateExternalBackendDevice()
    {
        return eastl::make_unique<FArdaExternalBackendDevice>();
    }

    bool RegisterExternalResourceProvider(IArdaExternalResourceProvider& Provider)
    {
        const char* Name = Provider.GetName();
        if (!Name || !Name[0])
        {
            SetBackendError("External resource provider name must be non-empty.");
            return false;
        }
        auto& Registry = GetResourceProviderRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        for (const auto& Entry : Registry.mEntries)
        {
            if (Entry.mName == Name)
            {
                if (Entry.mProvider == &Provider)
                {
                    SetBackendError("");
                    return true;
                }
                SetBackendError(
                    "An external resource provider with that name is already registered.");
                return false;
            }
        }
        Registry.mEntries.push_back({ Name, &Provider });
        SetBackendError("");
        return true;
    }

    bool UnregisterExternalResourceProvider(IArdaExternalResourceProvider& Provider)
    {
        const char* Name = Provider.GetName();
        if (!Name || !Name[0])
        {
            SetBackendError("External resource provider name must be non-empty.");
            return false;
        }
        auto& Registry = GetResourceProviderRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        for (auto It = Registry.mEntries.begin(); It != Registry.mEntries.end(); ++It)
        {
            if (It->mName != Name)
                continue;
            if (It->mProvider != &Provider)
            {
                SetBackendError(
                    "The named external resource provider is a different object.");
                return false;
            }
            Registry.mEntries.erase(It);
            SetBackendError("");
            return true;
        }
        SetBackendError("");
        return true;
    }

    const IArdaExternalResourceProvider* GetExternalResourceProvider(
        const char* Name) noexcept
    {
        if (!Name || !Name[0])
            return nullptr;
        auto& Registry = GetResourceProviderRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        for (const auto& Entry : Registry.mEntries)
            if (Entry.mName == Name)
                return Entry.mProvider;
        return nullptr;
    }

    rhi::TArdaRHIResult<rhi::FArdaRHITextureRef> ImportExternalTexture(
        const char* ProviderName, uint64_t Id)
    {
        return ImportExternalResource<
            rhi::FArdaRHITextureRef,
            rhi::FArdaRHINativeTextureImportDesc>(
            ProviderName,
            Id,
            &IArdaExternalResourceProvider::ResolveNativeTexture,
            &rhi::IArdaRHIDevice::ImportNativeTexture);
    }

    rhi::TArdaRHIResult<rhi::FArdaRHIBufferRef> ImportExternalBuffer(
        const char* ProviderName, uint64_t Id)
    {
        return ImportExternalResource<
            rhi::FArdaRHIBufferRef,
            rhi::FArdaRHINativeBufferImportDesc>(
            ProviderName,
            Id,
            &IArdaExternalResourceProvider::ResolveNativeBuffer,
            &rhi::IArdaRHIDevice::ImportNativeBuffer);
    }
}
