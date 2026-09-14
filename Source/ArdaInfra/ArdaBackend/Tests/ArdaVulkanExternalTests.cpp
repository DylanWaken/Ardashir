#include "ArdaTestValidation.h"
#include "ArdaBackend.h"
#include "ArdaExternalInterop.h"
#include <gtest/gtest.h>

#if defined(ARDA_TEST_NATIVE_VULKAN)
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <EASTL/weak_ptr.h>
#include <atomic>
#include <cstring>

namespace
{
	using namespace arda;

	// A genuine host device, created without any optional Arda/Vulkan features.
	struct FArdaVulkanHost
	{
		vk::detail::DynamicLoader mLoader;
		vk::detail::DispatchLoaderDynamic mDispatch;
		VkInstance mInstance = VK_NULL_HANDLE;
		VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
		VkDevice mDevice = VK_NULL_HANDLE;
		VkQueue mQueue = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT mMessenger = VK_NULL_HANDLE;
		uint32_t mFamily = 0;
		std::atomic<uint32_t> mErrors{0};
		eastl::string mError;

		static VKAPI_ATTR VkBool32 VKAPI_CALL Diagnostic(VkDebugUtilsMessageSeverityFlagBitsEXT Severity,
		    VkDebugUtilsMessageTypeFlagsEXT,
		    const VkDebugUtilsMessengerCallbackDataEXT* Data,
		    void* User)
		{
			if (Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				++static_cast<FArdaVulkanHost*>(User)->mErrors;
				std::fprintf(stderr, "Host Vulkan validation: %s\n", Data->pMessage);
			}
			return VK_FALSE;
		}

		bool Initialize()
		{
			auto GetProc = mLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
			if (!GetProc)
			{
				mError = "Vulkan loader unavailable";
				return false;
			}
			mDispatch.init(GetProc);
			uint32_t Count = 0;
#if ARDA_TEST_ENABLE_VALIDATION
			mDispatch.vkEnumerateInstanceLayerProperties(&Count, nullptr);
			std::vector<VkLayerProperties> Layers(Count);
			mDispatch.vkEnumerateInstanceLayerProperties(&Count, Layers.data());
			bool bValidation = false;
			for (const auto& Layer : Layers)
			{
				bValidation |= std::strcmp(Layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
			}
			if (!bValidation)
			{
				mError = "Vulkan validation unavailable";
				return false;
			}
			const char* Layer = "VK_LAYER_KHRONOS_validation";
			const char* Extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#endif
			VkApplicationInfo Application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
			Application.apiVersion = VK_API_VERSION_1_3;
			VkInstanceCreateInfo InstanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
			InstanceInfo.pApplicationInfo = &Application;
#if ARDA_TEST_ENABLE_VALIDATION
			InstanceInfo.enabledLayerCount = InstanceInfo.enabledExtensionCount = 1;
			InstanceInfo.ppEnabledLayerNames = &Layer;
			InstanceInfo.ppEnabledExtensionNames = &Extension;
#endif
			if (mDispatch.vkCreateInstance(&InstanceInfo, nullptr, &mInstance) != VK_SUCCESS)
			{
				return false;
			}
			mDispatch.init(vk::Instance(mInstance));
#if ARDA_TEST_ENABLE_VALIDATION
			VkDebugUtilsMessengerCreateInfoEXT Debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
			Debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			Debug.messageType =
			    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
			Debug.pfnUserCallback = Diagnostic;
			Debug.pUserData = this;
			if (mDispatch.vkCreateDebugUtilsMessengerEXT(mInstance, &Debug, nullptr, &mMessenger) != VK_SUCCESS)
			{
				return false;
			}
#endif
			mDispatch.vkEnumeratePhysicalDevices(mInstance, &Count, nullptr);
			std::vector<VkPhysicalDevice> Devices(Count);
			mDispatch.vkEnumeratePhysicalDevices(mInstance, &Count, Devices.data());
			for (const auto Device : Devices)
			{
				mDispatch.vkGetPhysicalDeviceQueueFamilyProperties(Device, &Count, nullptr);
				std::vector<VkQueueFamilyProperties> Families(Count);
				mDispatch.vkGetPhysicalDeviceQueueFamilyProperties(Device, &Count, Families.data());
				for (uint32_t I = 0; I < Count; ++I)
				{
					if ((Families[I].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
					    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
					{
						mPhysicalDevice = Device;
						mFamily = I;
						break;
					}
				}
				if (mPhysicalDevice)
				{
					break;
				}
			}
			if (!mPhysicalDevice)
			{
				mError = "No graphics/compute Vulkan device";
				return false;
			}
			VkPhysicalDeviceVulkan13Features Features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
			Features13.dynamicRendering = Features13.synchronization2 = VK_TRUE;
			VkPhysicalDeviceTimelineSemaphoreFeatures Timeline{
			    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
			Timeline.timelineSemaphore = VK_TRUE;
			Features13.pNext = &Timeline;
			const float Priority = 1.f;
			VkDeviceQueueCreateInfo QueueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
			QueueInfo.queueFamilyIndex = mFamily;
			QueueInfo.queueCount = 1;
			QueueInfo.pQueuePriorities = &Priority;
			VkDeviceCreateInfo DeviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
			DeviceInfo.pNext = &Features13;
			DeviceInfo.queueCreateInfoCount = 1;
			DeviceInfo.pQueueCreateInfos = &QueueInfo;
			if (mDispatch.vkCreateDevice(mPhysicalDevice, &DeviceInfo, nullptr, &mDevice) != VK_SUCCESS)
			{
				return false;
			}
			mDispatch.init(vk::Device(mDevice));
			mDispatch.vkGetDeviceQueue(mDevice, mFamily, 0, &mQueue);
			return true;
		}

		~FArdaVulkanHost()
		{
			if (mDevice)
			{
				mDispatch.vkDeviceWaitIdle(mDevice);
				mDispatch.vkDestroyDevice(mDevice, nullptr);
			}
			if (mMessenger)
			{
				mDispatch.vkDestroyDebugUtilsMessengerEXT(mInstance, mMessenger, nullptr);
			}
			if (mInstance)
			{
				mDispatch.vkDestroyInstance(mInstance, nullptr);
			}
			EXPECT_EQ(mErrors.load(), 0u);
		}
	};

	struct FArdaVulkanHostProvider : IArdaExternalDeviceProvider
	{
		eastl::shared_ptr<FArdaVulkanHost> mHost;
		FArdaExternalDeviceDesc mDesc;

		const char* GetBackendName() const noexcept override
		{
			return "native-vulkan";
		}

		bool GetExternalDeviceDesc(FArdaExternalDeviceDesc& Out) const override
		{
			Out = mDesc;
			return true;
		}

		eastl::shared_ptr<void> GetLifetimeToken() const override
		{
			return mHost;
		}
	};

	class FArdaVulkanExternal : public testing::Test
	{
	protected:
		FArdaVulkanHostProvider mProvider;

		void SetUp() override
		{
			ShutdownBackend();
			mProvider.mHost = eastl::make_shared<FArdaVulkanHost>();
			if (!mProvider.mHost->Initialize())
			{
				GTEST_SKIP() << mProvider.mHost->mError.c_str();
			}
			auto& D = mProvider.mDesc;
			D.mBackendName = "native-vulkan";
			D.mNativeApi = "vulkan";
			D.mInstance = FArdaNativeObject(mProvider.mHost->mInstance);
			D.mAdapter = FArdaNativeObject(mProvider.mHost->mPhysicalDevice);
			D.mDevice = FArdaNativeObject(mProvider.mHost->mDevice);
			D.mQueues.push_back(
			    {EArdaRHIQueueType::Graphics, FArdaNativeObject(mProvider.mHost->mQueue), mProvider.mHost->mFamily, 0});
			D.mProperties = {{"vulkan.api-version", "1.3"},
#if ARDA_TEST_ENABLE_VALIDATION
			    {"vulkan.validation", "enabled"},
#endif
			    {"vulkan.enabled-feature", "dynamicRendering"},
			    {"vulkan.enabled-feature", "synchronization2"},
			    {"vulkan.enabled-feature", "timelineSemaphore"}};
			ASSERT_TRUE(RegisterExternalDeviceProvider(mProvider));
			FArdaBackendConfiguration C = arda::MakeArdaTestBackendConfiguration();
			C.mBackendName = "native-vulkan";
			C.mDeviceSource = EArdaDeviceSource::ExternalProvider;
			C.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(C));
		}

		void TearDown() override
		{
			ShutdownBackend();
			EXPECT_TRUE(UnregisterExternalDeviceProvider(mProvider));
			EXPECT_TRUE(ConfigureBackend(arda::MakeArdaTestBackendConfiguration()));
		}
	};

	TEST_F(FArdaVulkanExternal, ExecutesAndRetainsHostBeyondShutdown)
	{
		ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
		auto Device = GetDevice();
		EXPECT_FALSE(Device->GetCapabilities().mResidency.mbSparseBinding);
		EXPECT_FALSE(Device->GetCapabilities().mDescriptors.mbRuntimeDescriptorArrays);
		EXPECT_FALSE(Device->GetCudaCapabilities());
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 1024;
		auto Buffer = Device->CreateBuffer(Desc);
		ASSERT_TRUE(Buffer) << Buffer.mStatus.mMessage.c_str();
		auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		eastl::vector<uint32_t> Values(256, 0x1234abcd);
		ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, Values.data(), 1024, 0));
		eastl::vector<uint8_t> Readback;
		ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Readback));
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
		ASSERT_EQ(Readback.size(), 1024u);
		EXPECT_EQ(std::memcmp(Values.data(), Readback.data(), 1024), 0);
		ASSERT_TRUE(Device->WaitForIdle());
		eastl::weak_ptr<FArdaVulkanHost> Weak = mProvider.mHost;
		mProvider.mHost.reset();
		ShutdownBackend();
		EXPECT_FALSE(Weak.expired());
		{
			auto Host = Weak.lock();
			ASSERT_TRUE(Host);
			VkFenceCreateInfo Info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
			VkFence Fence = VK_NULL_HANDLE;
			ASSERT_EQ(Host->mDispatch.vkCreateFence(Host->mDevice, &Info, nullptr, &Fence), VK_SUCCESS);
			Host->mDispatch.vkDestroyFence(Host->mDevice, Fence, nullptr);
		}
		Commands.mValue = nullptr;
		Buffer.mValue = nullptr;
		Device = nullptr;
		EXPECT_TRUE(Weak.expired());
	}

	TEST_F(FArdaVulkanExternal, RejectsMisalignedBufferDescriptorsBeforeNativeWrites)
	{
		ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
		auto Device = GetDevice();
		VkPhysicalDeviceProperties Properties{};
		mProvider.mHost->mDispatch.vkGetPhysicalDeviceProperties(mProvider.mHost->mPhysicalDevice, &Properties);
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 512;
		BufferDesc.mStructureStride = sizeof(uint32_t);
		BufferDesc.mUsage =
		    EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::Constant;
		auto Buffer = Device->CreateBuffer(BufferDesc);
		ASSERT_TRUE(Buffer);

		// Query the exact host adapter: an offset of one is legal on devices reporting alignment one.
		for (const auto Type : {EArdaRHIBindingType::StructuredBufferUAV, EArdaRHIBindingType::ConstantBuffer})
		{
			SCOPED_TRACE(static_cast<uint32_t>(Type));
			const auto Alignment = Type == EArdaRHIBindingType::ConstantBuffer
			    ? Properties.limits.minUniformBufferOffsetAlignment
			    : Properties.limits.minStorageBufferOffsetAlignment;
			ASSERT_GT(Alignment, 0u);
			FArdaRHIBindingLayoutDesc LayoutDesc;
			LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
			LayoutDesc.mItems = {{0, 1, Type}};
			auto Layout = Device->CreateBindingLayout(LayoutDesc);
			ASSERT_TRUE(Layout);
			FArdaRHIBindlessLayoutDesc BindlessDesc;
			BindlessDesc.mVisibility = EArdaRHIShaderStage::Compute;
			BindlessDesc.mMaxCapacity = 1;
			BindlessDesc.mRegisterSpaces = {{0, 1, Type}};
			auto Bindless = Device->CreateBindlessLayout(BindlessDesc);
			ASSERT_TRUE(Bindless);
			auto Table = Device->CreateDescriptorTable(Bindless.mValue);
			ASSERT_TRUE(Table);
			FArdaRHIBindingItem Item;
			Item.mType = Type;
			Item.mResource = FArdaRHIResourceRef(Buffer.mValue.Get());
			Item.mView.mBufferRange = {Alignment, 16};
			FArdaRHIBindingSetDesc SetDesc;
			SetDesc.mLayout = Layout.mValue;
			SetDesc.mItems = {Item};
			ASSERT_TRUE(Device->CreateBindingSet(SetDesc));
			ASSERT_TRUE(Device->WriteDescriptorTable(Table.mValue, Item));
			const auto OriginalRange = Table.mValue->GetDesc().mItems[0].mView;

			if (Alignment > 1)
			{
				// A typed UAV's retained range must be checked after canonicalization, before native mutation.
				Item.mView.mBufferRange.mByteOffset = Alignment + 1;
				if (Type == EArdaRHIBindingType::StructuredBufferUAV)
				{
					auto View = Device->CreateUnorderedAccessView(FArdaRHIResourceRef(Buffer.mValue.Get()), Item.mView);
					ASSERT_TRUE(View);
					Item.mResource = FArdaRHIResourceRef(View.mValue.Get());
					Item.mView = {};
				}
				SetDesc.mItems = {Item};
				const auto InvalidSet = Device->CreateBindingSet(SetDesc);
				EXPECT_EQ(InvalidSet.mStatus.mCode, EArdaRHIResult::InvalidArgument);
				EXPECT_NE(InvalidSet.mStatus.mMessage.find("alignment"), eastl::string::npos);
				const auto InvalidWrite = Device->WriteDescriptorTable(Table.mValue, Item);
				EXPECT_EQ(InvalidWrite.mCode, EArdaRHIResult::InvalidArgument);
				EXPECT_NE(InvalidWrite.mMessage.find("alignment"), eastl::string::npos);
				ASSERT_EQ(Table.mValue->GetDesc().mItems.size(), 1u);
				EXPECT_EQ(Table.mValue->GetDesc().mItems[0].mView, OriginalRange);
			}
		}
		EXPECT_EQ(mProvider.mHost->mErrors.load(), 0u);
	}

	TEST_F(FArdaVulkanExternal, RejectsMissingEnabledFeatureWithoutDestroyingHost)
	{
		mProvider.mDesc.mProperties.pop_back();
		EXPECT_FALSE(InitializeBackend());
		EXPECT_NE(GetBackendError().find("timelineSemaphore"), eastl::string::npos);
		mProvider.mDesc.mProperties.push_back({"vulkan.enabled-feature", "timelineSemaphore"});
		ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
	}

	TEST_F(FArdaVulkanExternal, RejectsMalformedQueuesWithoutDestroyingHost)
	{
		mProvider.mDesc.mQueues.push_back(mProvider.mDesc.mQueues.front());
		EXPECT_FALSE(InitializeBackend());
		EXPECT_NE(GetBackendError().find("queue"), eastl::string::npos);
		mProvider.mDesc.mQueues.pop_back();
		mProvider.mDesc.mQueues.front().mFamilyIndex = UINT32_MAX;
		EXPECT_FALSE(InitializeBackend());
		mProvider.mDesc.mQueues.front().mFamilyIndex = mProvider.mHost->mFamily;
		ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
	}
}
#endif
