#include "ArdaTestValidation.h"
#include "ArdaBackend.h"
#include "RHI/Interop/ArdaExternalInterop.h"
#include <gtest/gtest.h>

#if defined(ARDA_TEST_NATIVE_VULKAN)
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#define VK_ENABLE_BETA_EXTENSIONS
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <EASTL/weak_ptr.h>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>

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
		bool mbMeshEnabled = false;

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

		bool Initialize(bool bMeshOnly = false)
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
			VkPhysicalDeviceMeshShaderFeaturesEXT Mesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
			const char* MeshExtension = VK_EXT_MESH_SHADER_EXTENSION_NAME;
			if (bMeshOnly)
			{
				uint32_t ExtensionCount = 0;
				mDispatch.vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &ExtensionCount, nullptr);
				std::vector<VkExtensionProperties> Extensions(ExtensionCount);
				mDispatch.vkEnumerateDeviceExtensionProperties(mPhysicalDevice,
				    nullptr,
				    &ExtensionCount,
				    Extensions.data());
				bool bExtension = false;
				for (const auto& Extension : Extensions)
				{
					bExtension |= std::strcmp(Extension.extensionName, MeshExtension) == 0;
				}
				VkPhysicalDeviceFeatures2 Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
				Features.pNext = &Mesh;
				mDispatch.vkGetPhysicalDeviceFeatures2(mPhysicalDevice, &Features);
				if (!bExtension || !Mesh.meshShader)
				{
					mError = "Mesh shaders are unavailable on the host device.";
					return false;
				}
				Mesh = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
				Mesh.meshShader = VK_TRUE;
				Timeline.pNext = &Mesh;
				DeviceInfo.enabledExtensionCount = 1;
				DeviceInfo.ppEnabledExtensionNames = &MeshExtension;
				mbMeshEnabled = true;
			}
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

		virtual bool EnableMeshStage() const
		{
			return false;
		}

		void SetUp() override
		{
			ShutdownBackend();
			mProvider.mHost = eastl::make_shared<FArdaVulkanHost>();
			if (!mProvider.mHost->Initialize(EnableMeshStage()))
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
			if (mProvider.mHost->mbMeshEnabled)
			{
				D.mProperties.push_back({"vulkan.device-extension", VK_EXT_MESH_SHADER_EXTENSION_NAME});
				D.mProperties.push_back({"vulkan.enabled-feature", "meshShader"});
			}
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

	class FArdaVulkanExternalAuditHooks
	{
	public:
		FArdaVulkanExternalAuditHooks()
		    : mOriginal(VULKAN_HPP_DEFAULT_DISPATCHER)
		{
			mActive = this;
			VULKAN_HPP_DEFAULT_DISPATCHER.vkQueueWaitIdle = QueueWaitIdle;
			VULKAN_HPP_DEFAULT_DISPATCHER.vkDeviceWaitIdle = DeviceWaitIdle;
			VULKAN_HPP_DEFAULT_DISPATCHER.vkGetFenceStatus = GetFenceStatus;
			VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdDrawIndirect = DrawIndirect;
		}

		~FArdaVulkanExternalAuditHooks()
		{
			VULKAN_HPP_DEFAULT_DISPATCHER = mOriginal;
			mActive = nullptr;
		}

		bool mbBlockRetirement = false;
		std::vector<uint32_t> mDrawCounts;
		std::vector<uint64_t> mDrawOffsets;
		vk::detail::DispatchLoaderDynamic mOriginal;

	private:
		static inline FArdaVulkanExternalAuditHooks* mActive = nullptr;

		static VKAPI_ATTR VkResult VKAPI_CALL QueueWaitIdle(VkQueue Queue)
		{
			return mActive->mbBlockRetirement ? VK_ERROR_OUT_OF_HOST_MEMORY : mActive->mOriginal.vkQueueWaitIdle(Queue);
		}

		static VKAPI_ATTR VkResult VKAPI_CALL DeviceWaitIdle(VkDevice Device)
		{
			return mActive->mbBlockRetirement ? VK_ERROR_OUT_OF_HOST_MEMORY
			                                  : mActive->mOriginal.vkDeviceWaitIdle(Device);
		}

		static VKAPI_ATTR VkResult VKAPI_CALL GetFenceStatus(VkDevice Device, VkFence Fence)
		{
			return mActive->mbBlockRetirement ? VK_ERROR_OUT_OF_HOST_MEMORY
			                                  : mActive->mOriginal.vkGetFenceStatus(Device, Fence);
		}

		static VKAPI_ATTR void VKAPI_CALL
		DrawIndirect(VkCommandBuffer Commands, VkBuffer Buffer, VkDeviceSize Offset, uint32_t Count, uint32_t Stride)
		{
			mActive->mDrawCounts.push_back(Count);
			mActive->mDrawOffsets.push_back(Offset);
			mActive->mOriginal.vkCmdDrawIndirect(Commands, Buffer, Offset, Count, Stride);
		}
	};

	class FArdaVulkanMeshOnlyExternal : public FArdaVulkanExternal
	{
		bool EnableMeshStage() const override
		{
			return true;
		}
	};

	TEST_F(FArdaVulkanMeshOnlyExternal, ReportsMeshWithoutDisabledAmplification)
	{
		ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
		EXPECT_EQ(GetDevice()->GetCapabilities().mMeshShaderTier, EArdaRHIMeshShaderTier::MeshShadersOnly);
		EXPECT_FALSE(GetDevice()->GetCapabilities().mbIndirectFirstInstance);
	}

	TEST_F(FArdaVulkanExternal, LowersMultiDrawAndResumesRenderingAfterTransfer)
	{
		ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
		auto Device = GetDevice();
		EXPECT_FALSE(Device->GetCapabilities().mbIndirectFirstInstance);
		const auto LoadShader = [&](const char* File, const char* Entry, EArdaRHIShaderStage Stage)
		{
			std::ifstream Stream(std::string(ARDA_BACKEND_TEST_SHADER_DIR) + "/" + File + ".spv", std::ios::binary);
			std::vector<uint8_t> Bytes{std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
			FArdaRHIShaderDesc Desc;
			Desc.mStage = Stage;
			Desc.mEntryPoint = Entry;
			Desc.mBytecode = Bytes.data();
			Desc.mBytecodeSize = Bytes.size();
			return Device->CreateShader(Desc);
		};
		auto Vertex = LoadShader("ArdaRasterStageVS", "RasterStageVS", EArdaRHIShaderStage::Vertex);
		auto Pixel = LoadShader("ArdaRasterStagePS", "RasterStagePS", EArdaRHIShaderStage::Pixel);
		ASSERT_TRUE(Vertex);
		ASSERT_TRUE(Pixel);
		FArdaRHIGraphicsPipelineDesc PipelineDesc;
		PipelineDesc.mVertexShader = Vertex.mValue;
		PipelineDesc.mPixelShader = Pixel.mValue;
		PipelineDesc.mColorFormats = {EArdaRHIFormat::RGBA8UNorm};
		PipelineDesc.mRasterState.mCullMode = EArdaRHICullMode::None;
		PipelineDesc.mDepthStencilState.mbDepthTest = false;
		PipelineDesc.mDepthStencilState.mbDepthWrite = false;
		auto Pipeline = Device->CreateGraphicsPipeline(PipelineDesc);
		ASSERT_TRUE(Pipeline);
		FArdaRHITextureDesc TargetDesc;
		TargetDesc.mWidth = TargetDesc.mHeight = 8;
		TargetDesc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		TargetDesc.mArraySize = 2;
		TargetDesc.mMipLevels = 2;
		TargetDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		TargetDesc.mUsage = EArdaRHITextureUsage::RenderTarget;
		auto Target = Device->CreateTexture(TargetDesc);
		ASSERT_TRUE(Target);
		FArdaRHIStagingTextureDesc ReadbackDesc;
		ReadbackDesc.mTexture = TargetDesc;
		ReadbackDesc.mTexture.mWidth = ReadbackDesc.mTexture.mHeight = 4;
		ReadbackDesc.mTexture.mDimension = EArdaRHITextureDimension::Texture2D;
		ReadbackDesc.mTexture.mArraySize = ReadbackDesc.mTexture.mMipLevels = 1;
		ReadbackDesc.mCpuAccess = EArdaRHICpuAccess::Read;
		auto Readback = Device->CreateStagingTexture(ReadbackDesc);
		ASSERT_TRUE(Readback);
		FArdaRHIFramebufferDesc FramebufferDesc;
		FArdaRHIFramebufferAttachment Attachment;
		Attachment.mSubresources.mBaseMipLevel = 1;
		Attachment.mSubresources.mMipLevelCount = 1;
		Attachment.mSubresources.mBaseArraySlice = 1;
		Attachment.mSubresources.mArraySliceCount = 1;
		FramebufferDesc.mColorAttachments.push_back({Target.mValue, Attachment});
		auto Framebuffer = Device->CreateFramebuffer(FramebufferDesc);
		ASSERT_TRUE(Framebuffer);
		const uint32_t Arguments[] = {3, 1, 0, 0, 3, 1, 0, 0, 3, 1, 0, 0};
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = sizeof(Arguments);
		BufferDesc.mUsage = EArdaRHIBufferUsage::Indirect;
		auto Buffer = Device->CreateBuffer(BufferDesc);
		ASSERT_TRUE(Buffer);
		FArdaRHIBufferDesc ScratchDesc;
		ScratchDesc.mByteSize = sizeof(uint32_t);
		auto Scratch = Device->CreateBuffer(ScratchDesc);
		ASSERT_TRUE(Scratch);
		auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, Arguments, sizeof(Arguments)));
		ASSERT_TRUE(Commands.mValue->SetBufferState(*Buffer.mValue, EArdaRHIResourceState::IndirectArgument));
		FArdaRHIGraphicsState State;
		State.mPipeline = Pipeline.mValue;
		State.mFramebuffer = Framebuffer.mValue;
		ASSERT_TRUE(Commands.mValue->ClearTexture(*Target.mValue, {}, {0, 0, 1, 1}));
		ASSERT_TRUE(Commands.mValue->SetGraphicsState(State));
		FArdaVulkanExternalAuditHooks Hooks;
		ASSERT_TRUE(Commands.mValue->DrawIndirect(*Buffer.mValue, 0, 3, 16));
		const uint32_t Value = 42;
		ASSERT_TRUE(Commands.mValue->WriteBuffer(*Scratch.mValue, &Value, sizeof(Value)));
		ASSERT_TRUE(Commands.mValue->DrawIndirect(*Buffer.mValue, 0, 3, 16));
		EXPECT_EQ(Hooks.mDrawCounts, (std::vector<uint32_t>{1, 1, 1, 1, 1, 1}));
		EXPECT_EQ(Hooks.mDrawOffsets, (std::vector<uint64_t>{0, 16, 32, 0, 16, 32}));
		FArdaRHITextureSlice Slice;
		Slice.mWidth = Slice.mHeight = 4;
		Slice.mDepth = 1;
		auto SourceSlice = Slice;
		SourceSlice.mMipLevel = SourceSlice.mArraySlice = 1;
		ASSERT_TRUE(Commands.mValue->CopyTextureToStaging(*Readback.mValue, Slice, *Target.mValue, SourceSlice));
		ASSERT_TRUE(Commands.mValue->Close());
		const auto Submitted = Device->ExecuteCommandList(Commands.mValue);
		ASSERT_TRUE(Submitted);
		ASSERT_TRUE(Device->WaitForSubmission(Submitted.mValue));
		auto Mapping = Device->MapStagingTexture(Readback.mValue, Slice, EArdaRHICpuAccess::Read);
		ASSERT_TRUE(Mapping);
		for (uint32_t Y = 0; Y < 4; ++Y)
		{
			const auto* Row = static_cast<const uint8_t*>(Mapping.mValue.mData) + Y * Mapping.mValue.mRowPitch;
			for (uint32_t X = 0; X < 4; ++X)
			{
				EXPECT_EQ(Row[X * 4], 255u);
				EXPECT_EQ(Row[X * 4 + 1], 0u);
				EXPECT_EQ(Row[X * 4 + 2], 0u);
				EXPECT_EQ(Row[X * 4 + 3], 255u);
			}
		}
		ASSERT_TRUE(Device->UnmapStagingTexture(Readback.mValue));
	}

	TEST_F(FArdaVulkanExternal, ShutdownQuarantineRetainsHostUntilLaterInitializationProvesCompletion)
	{
		ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
		auto Device = GetDevice();
		auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
		// Actual work is drained, but the injected failures withhold completion proof from Arda.
		ASSERT_EQ(mProvider.mHost->mDispatch.vkDeviceWaitIdle(mProvider.mHost->mDevice), VK_SUCCESS);
		eastl::weak_ptr<FArdaVulkanHost> Previous = mProvider.mHost;
		{
			FArdaVulkanExternalAuditHooks Hooks;
			Hooks.mbBlockRetirement = true;
			Commands.mValue = {};
			Device = {};
			ShutdownBackend();
			mProvider.mHost.reset();
			EXPECT_FALSE(Previous.expired());
		}
		mProvider.mHost = eastl::make_shared<FArdaVulkanHost>();
		ASSERT_TRUE(mProvider.mHost->Initialize());
		mProvider.mDesc.mInstance = FArdaNativeObject(mProvider.mHost->mInstance);
		mProvider.mDesc.mAdapter = FArdaNativeObject(mProvider.mHost->mPhysicalDevice);
		mProvider.mDesc.mDevice = FArdaNativeObject(mProvider.mHost->mDevice);
		mProvider.mDesc.mQueues = {
		    {EArdaRHIQueueType::Graphics, FArdaNativeObject(mProvider.mHost->mQueue), mProvider.mHost->mFamily, 0}};
		ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
		EXPECT_TRUE(Previous.expired());
	}

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
