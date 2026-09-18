#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"

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

#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

namespace arda
{
	namespace
	{
		// Match the provider's platform/beta definitions: the default dispatcher's layout depends on them.
		class FArdaVulkanAuditHooks
		{
		public:
			FArdaVulkanAuditHooks()
			    : mOriginal(VULKAN_HPP_DEFAULT_DISPATCHER)
			{
				mActive = this;
				auto& Dispatch = VULKAN_HPP_DEFAULT_DISPATCHER;
				Dispatch.vkBeginCommandBuffer = BeginCommandBuffer;
				Dispatch.vkCreateFence = CreateFence;
				Dispatch.vkDestroyFence = DestroyFence;
				Dispatch.vkWaitForFences = WaitForFences;
				Dispatch.vkGetFenceStatus = GetFenceStatus;
				Dispatch.vkQueueBindSparse = BindSparse;
				Dispatch.vkAllocateMemory = AllocateMemory;
				Dispatch.vkFreeMemory = FreeMemory;
				Dispatch.vkCreateBuffer = CreateBuffer;
				Dispatch.vkDestroyBuffer = DestroyBuffer;
				Dispatch.vkQueueSubmit2 = Submit;
			}

			~FArdaVulkanAuditHooks()
			{
				VULKAN_HPP_DEFAULT_DISPATCHER = mOriginal;
				mActive = nullptr;
			}

			vk::detail::DispatchLoaderDynamic mOriginal;
			bool mbFailCreateFence = false;
			bool mbFailBindSparse = false;
			bool mbFailWaitAfterCompletion = false;
			bool mbFailWaitBeforeCompletion = false;
			bool mbFailCreateBuffer = false;
			bool mbCaptureNativeAllocations = false;
			VkDevice mNativeDevice = VK_NULL_HANDLE;
			VkFence mWithheldFence = VK_NULL_HANDLE;
			uint32_t mDestroyedFences = 0;
			uint32_t mWaitCalls = 0;
			uint32_t mAllocations = 0;
			uint32_t mFrees = 0;
			std::vector<VkCommandBuffer> mBegunBuffers;
			std::vector<VkFence> mCreatedFences;
			std::vector<VkDeviceMemory> mAllocatedMemory;
			std::vector<VkBuffer> mDestroyedBuffers;
			std::vector<uint64_t> mTimelineValues;

		private:
			static inline FArdaVulkanAuditHooks* mActive = nullptr;

			static VKAPI_ATTR VkResult VKAPI_CALL BeginCommandBuffer(VkCommandBuffer Buffer,
			    const VkCommandBufferBeginInfo* Info)
			{
				mActive->mBegunBuffers.push_back(Buffer);
				return mActive->mOriginal.vkBeginCommandBuffer(Buffer, Info);
			}

			static VKAPI_ATTR VkResult VKAPI_CALL CreateFence(VkDevice Device,
			    const VkFenceCreateInfo* Info,
			    const VkAllocationCallbacks* Allocator,
			    VkFence* Fence)
			{
				const auto Result = mActive->mbFailCreateFence
				    ? VK_ERROR_OUT_OF_HOST_MEMORY
				    : mActive->mOriginal.vkCreateFence(Device, Info, Allocator, Fence);
				if (Result == VK_SUCCESS && mActive->mbCaptureNativeAllocations)
				{
					mActive->mNativeDevice = Device;
					mActive->mCreatedFences.push_back(*Fence);
				}
				return Result;
			}

			static VKAPI_ATTR void VKAPI_CALL DestroyFence(VkDevice Device,
			    VkFence Fence,
			    const VkAllocationCallbacks* Allocator)
			{
				++mActive->mDestroyedFences;
				mActive->mOriginal.vkDestroyFence(Device, Fence, Allocator);
			}

			static VKAPI_ATTR VkResult VKAPI_CALL
			WaitForFences(VkDevice Device, uint32_t Count, const VkFence* Fences, VkBool32 bWaitAll, uint64_t Timeout)
			{
				++mActive->mWaitCalls;
				if (mActive->mbFailWaitBeforeCompletion)
				{
					return VK_ERROR_OUT_OF_HOST_MEMORY;
				}
				if (mActive->mbFailBindSparse)
				{
					// A broken failure path must fail the test instead of hanging on an unsignaled fence.
					return VK_ERROR_DEVICE_LOST;
				}
				const auto Result = mActive->mOriginal.vkWaitForFences(Device, Count, Fences, bWaitAll, Timeout);
				return Result == VK_SUCCESS && mActive->mbFailWaitAfterCompletion ? VK_ERROR_DEVICE_LOST : Result;
			}

			static VKAPI_ATTR VkResult VKAPI_CALL GetFenceStatus(VkDevice Device, VkFence Fence)
			{
				return Fence == mActive->mWithheldFence ? VK_NOT_READY
				                                        : mActive->mOriginal.vkGetFenceStatus(Device, Fence);
			}

			static VKAPI_ATTR VkResult VKAPI_CALL BindSparse(VkQueue Queue,
			    uint32_t Count,
			    const VkBindSparseInfo* Info,
			    VkFence Fence)
			{
				return mActive->mbFailBindSparse ? VK_ERROR_OUT_OF_DEVICE_MEMORY
				                                 : mActive->mOriginal.vkQueueBindSparse(Queue, Count, Info, Fence);
			}

			static VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(VkDevice Device,
			    const VkMemoryAllocateInfo* Info,
			    const VkAllocationCallbacks* Allocator,
			    VkDeviceMemory* Memory)
			{
				const auto Result = mActive->mOriginal.vkAllocateMemory(Device, Info, Allocator, Memory);
				mActive->mAllocations += Result == VK_SUCCESS;
				if (Result == VK_SUCCESS && mActive->mbCaptureNativeAllocations)
				{
					mActive->mAllocatedMemory.push_back(*Memory);
				}
				return Result;
			}

			static VKAPI_ATTR void VKAPI_CALL FreeMemory(VkDevice Device,
			    VkDeviceMemory Memory,
			    const VkAllocationCallbacks* Allocator)
			{
				++mActive->mFrees;
				mActive->mOriginal.vkFreeMemory(Device, Memory, Allocator);
			}

			static VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice Device,
			    const VkBufferCreateInfo* Info,
			    const VkAllocationCallbacks* Allocator,
			    VkBuffer* Buffer)
			{
				return mActive->mbFailCreateBuffer ? VK_ERROR_OUT_OF_DEVICE_MEMORY
				                                   : mActive->mOriginal.vkCreateBuffer(Device, Info, Allocator, Buffer);
			}

			static VKAPI_ATTR void VKAPI_CALL DestroyBuffer(VkDevice Device,
			    VkBuffer Buffer,
			    const VkAllocationCallbacks* Allocator)
			{
				mActive->mDestroyedBuffers.push_back(Buffer);
				mActive->mOriginal.vkDestroyBuffer(Device, Buffer, Allocator);
			}

			static VKAPI_ATTR VkResult VKAPI_CALL Submit(VkQueue Queue,
			    uint32_t Count,
			    const VkSubmitInfo2* Info,
			    VkFence Fence)
			{
				// Native queue calls are externally synchronized by the provider.
				for (uint32_t Index = 0; Index < Count; ++Index)
				{
					for (uint32_t Signal = 0; Signal < Info[Index].signalSemaphoreInfoCount; ++Signal)
					{
						const auto Value = Info[Index].pSignalSemaphoreInfos[Signal].value;
						if (Value)
						{
							mActive->mTimelineValues.push_back(Value);
						}
					}
				}
				return mActive->mOriginal.vkQueueSubmit2(Queue, Count, Info, Fence);
			}
		};

		class FArdaVulkanLifetimeAuditTest : public testing::Test
		{
		protected:
			void SetUp() override
			{
				ShutdownBackend();
				auto Configuration = MakeArdaTestBackendConfiguration();
				Configuration.mBackendName = "native-vulkan";
				Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
				Configuration.mMessageCallback = &mDiagnostics;
				ASSERT_TRUE(ConfigureBackend(Configuration));
				ARDA_REQUIRE_BACKEND();
				mDevice = GetDevice();
				ASSERT_TRUE(mDevice);
			}

			void TearDown() override
			{
				if (mDevice)
				{
					EXPECT_TRUE(mDevice->WaitForIdle());
				}
				mDevice = {};
				ShutdownBackend();
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
				(void)ConfigureBackend(MakeArdaTestBackendConfiguration());
			}

			FArdaTestDiagnosticCallback mDiagnostics;
			FArdaRHIDeviceRef mDevice;
		};

		TEST_F(FArdaVulkanLifetimeAuditTest, DestroysFenceWhenCompletionWaitReportsDeviceLoss)
		{
			auto Fence = mDevice->CreateGpuFence();
			ASSERT_TRUE(Fence);
			ASSERT_TRUE(mDevice->SignalGpuFence(Fence.mValue, EArdaRHIQueueType::Graphics));
			FArdaVulkanAuditHooks Hooks;
			Hooks.mbFailWaitAfterCompletion = true;
			Fence.mValue = {};
			EXPECT_EQ(Hooks.mWaitCalls, 1u);
			EXPECT_EQ(Hooks.mDestroyedFences, 1u);
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, RecoverableWaitFailureDoesNotDestroySubmittedFence)
		{
			FArdaVulkanAuditHooks Hooks;
			Hooks.mbCaptureNativeAllocations = true;
			auto Fence = mDevice->CreateGpuFence();
			ASSERT_TRUE(Fence);
			ASSERT_TRUE(mDevice->SignalGpuFence(Fence.mValue, EArdaRHIQueueType::Graphics));
			ASSERT_EQ(Hooks.mCreatedFences.size(), 1u);
			Hooks.mbFailWaitBeforeCompletion = true;
			Fence.mValue = {};
			EXPECT_EQ(Hooks.mWaitCalls, 1u);
			EXPECT_EQ(Hooks.mDestroyedFences, 0u);
			// Establish completion without the injected wait error; provider GC must reclaim the owned fence.
			EXPECT_EQ(Hooks.mOriginal
			              .vkWaitForFences(Hooks.mNativeDevice, 1, Hooks.mCreatedFences.data(), VK_TRUE, UINT64_MAX),
			    VK_SUCCESS);
			mDevice->RunGarbageCollection();
			EXPECT_EQ(Hooks.mDestroyedFences, 1u);
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, ReopeningSubmittedListPreservesItsNativeRecording)
		{
			auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			FArdaVulkanAuditHooks Hooks;
			ASSERT_TRUE(Commands.mValue->Open());
			ASSERT_TRUE(Commands.mValue->Close());
			const auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
			ASSERT_TRUE(Submitted);
			ASSERT_TRUE(Commands.mValue->Open());
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_EQ(Hooks.mBegunBuffers.size(), 2u);
			EXPECT_NE(Hooks.mBegunBuffers[0], Hooks.mBegunBuffers[1]);
			ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, SparseCommitFailureReleasesTemporaryNativeObjects)
		{
			if (!mDevice->GetCapabilities().mResidency.mbReservedBuffers)
			{
				GTEST_SKIP() << "Vulkan sparse buffers are unavailable.";
			}
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 65536;
			Desc.mbTiled = true;
			auto Buffer = mDevice->CreateBuffer(Desc);
			ASSERT_TRUE(Buffer);
			FArdaVulkanAuditHooks Hooks;
			Hooks.mbFailCreateFence = true;
			auto Status = mDevice->CommitReservedResource(FArdaRHIResourceRef(Buffer.mValue.Get()),
			    Desc.mByteSize,
			    EArdaRHIQueueType::Graphics);
			EXPECT_EQ(Status.mCode, EArdaRHIResult::BackendFailure);
			EXPECT_EQ(Hooks.mAllocations, 1u);
			EXPECT_EQ(Hooks.mFrees, Hooks.mAllocations);
			Hooks.mbFailCreateFence = false;
			Hooks.mbFailBindSparse = true;
			Status = mDevice->CommitReservedResource(FArdaRHIResourceRef(Buffer.mValue.Get()),
			    Desc.mByteSize,
			    EArdaRHIQueueType::Graphics);
			EXPECT_EQ(Status.mCode, EArdaRHIResult::BackendFailure);
			EXPECT_EQ(Hooks.mAllocations, 2u);
			EXPECT_EQ(Hooks.mFrees, Hooks.mAllocations);
			EXPECT_EQ(Hooks.mDestroyedFences, 1u);
			EXPECT_EQ(Hooks.mWaitCalls, 0u);
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, RecoverableSparseWaitFailureDoesNotFreeSubmittedMemory)
		{
			if (!mDevice->GetCapabilities().mResidency.mbReservedBuffers)
			{
				GTEST_SKIP() << "Vulkan sparse buffers are unavailable.";
			}
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 65536;
			Desc.mbTiled = true;
			auto Buffer = mDevice->CreateBuffer(Desc);
			ASSERT_TRUE(Buffer);
			FArdaVulkanAuditHooks Hooks;
			Hooks.mbCaptureNativeAllocations = true;
			Hooks.mbFailWaitBeforeCompletion = true;
			const auto Status = mDevice->CommitReservedResource(FArdaRHIResourceRef(Buffer.mValue.Get()),
			    Desc.mByteSize,
			    EArdaRHIQueueType::Graphics);
			EXPECT_EQ(Status.mCode, EArdaRHIResult::BackendFailure);
			EXPECT_EQ(Hooks.mWaitCalls, 1u);
			EXPECT_EQ(Hooks.mFrees, 0u);
			EXPECT_EQ(Hooks.mDestroyedFences, 0u);
			ASSERT_EQ(Hooks.mCreatedFences.size(), 1u);
			ASSERT_EQ(Hooks.mAllocatedMemory.size(), 1u);
			EXPECT_EQ(Hooks.mOriginal
			              .vkWaitForFences(Hooks.mNativeDevice, 1, Hooks.mCreatedFences.data(), VK_TRUE, UINT64_MAX),
			    VK_SUCCESS);
			Hooks.mbFailWaitBeforeCompletion = false;
			Hooks.mbCaptureNativeAllocations = false;
			mDevice->RunGarbageCollection();
			EXPECT_EQ(Hooks.mDestroyedFences, 1u);
			EXPECT_EQ(Hooks.mFrees, 0u);
			// The accepted mapping owns its allocation despite the wait error; unbinding releases it.
			EXPECT_TRUE(mDevice->CommitReservedResource(FArdaRHIResourceRef(Buffer.mValue.Get()),
			    0,
			    EArdaRHIQueueType::Graphics));
			EXPECT_EQ(Hooks.mFrees, 1u);
			EXPECT_EQ(Hooks.mDestroyedFences, 2u);
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, SparsePrefixGrowthShrinkAndClampingPreserveExistingData)
		{
			if (!mDevice->GetCapabilities().mResidency.mbReservedBuffers)
			{
				GTEST_SKIP() << "Vulkan sparse buffers are unavailable.";
			}
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 1024 * 1024;
			Desc.mbTiled = true;
			auto Buffer = mDevice->CreateBuffer(Desc);
			ASSERT_TRUE(Buffer);
			const auto Requirements = mDevice->GetBufferMemoryRequirements(Buffer.mValue);
			ASSERT_TRUE(Requirements);
			const uint64_t Tile = Requirements.mValue.mAlignment;
			ASSERT_LE(Tile * 2, Desc.mByteSize);
			ASSERT_TRUE(mDevice->CommitReservedResource(FArdaRHIResourceRef(Buffer.mValue.Get()),
			    Tile,
			    EArdaRHIQueueType::Graphics));
			const uint32_t Expected = 0xBADC0DE;
			auto Write = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Write);
			ASSERT_TRUE(Write.mValue->Open());
			ASSERT_TRUE(Write.mValue->WriteBuffer(*Buffer.mValue, &Expected, sizeof(Expected), 0));
			ASSERT_TRUE(Write.mValue->Close());
			const auto Written = mDevice->ExecuteCommandList(Write.mValue);
			ASSERT_TRUE(Written);
			ASSERT_TRUE(mDevice->WaitForSubmission(Written.mValue));
			FArdaVulkanAuditHooks Hooks;
			for (const uint64_t Size : {Tile, Tile * 2, Tile, uint64_t{UINT64_MAX}})
			{
				ASSERT_TRUE(mDevice->CommitReservedResource(FArdaRHIResourceRef(Buffer.mValue.Get()),
				    Size,
				    EArdaRHIQueueType::Graphics));
				auto Read = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Read);
				ASSERT_TRUE(Read.mValue->Open());
				eastl::vector<uint8_t> Readback;
				ASSERT_TRUE(Read.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Readback, 0, sizeof(Expected)));
				ASSERT_TRUE(Read.mValue->Close());
				const auto Submitted = mDevice->ExecuteCommandList(Read.mValue);
				ASSERT_TRUE(Submitted);
				ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
				ASSERT_EQ(Readback.size(), sizeof(Expected));
				uint32_t Actual = 0;
				std::memcpy(&Actual, Readback.data(), sizeof(Actual));
				EXPECT_EQ(Actual, Expected) << "Committed size " << Size;
			}
			ASSERT_TRUE(mDevice->CommitReservedResource(FArdaRHIResourceRef(Buffer.mValue.Get()),
			    0,
			    EArdaRHIQueueType::Graphics));
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, SparseBufferOwnersRetireAfterTheirLastMappedRange)
		{
			if (!mDevice->GetCapabilities().mResidency.mbReservedBuffers)
			{
				GTEST_SKIP() << "Vulkan sparse buffers are unavailable.";
			}
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 1024 * 1024;
			Desc.mbTiled = true;
			auto Buffer = mDevice->CreateBuffer(Desc);
			ASSERT_TRUE(Buffer);
			const auto Requirements = mDevice->GetBufferMemoryRequirements(Buffer.mValue);
			ASSERT_TRUE(Requirements);
			const uint64_t Tile = Requirements.mValue.mAlignment;
			FArdaRHIHeapDesc HeapDesc;
			HeapDesc.mCapacity = Tile * 2;
			HeapDesc.mMemoryTypeBits = Requirements.mValue.mMemoryTypeBits;
			FArdaVulkanAuditHooks Hooks;
			auto First = mDevice->CreateHeap(HeapDesc);
			auto Second = mDevice->CreateHeap(HeapDesc);
			ASSERT_TRUE(First);
			ASSERT_TRUE(Second);
			FArdaRHIBufferTileMapping Mapping;
			Mapping.mByteSize = Tile * 2;
			Mapping.mHeap = First.mValue;
			ASSERT_TRUE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
			First.mValue = {};
			Mapping.mByteSize = Tile;
			Mapping.mHeap = Second.mValue;
			ASSERT_TRUE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
			Second.mValue = {};
			Mapping.mHeap = {};
			EXPECT_EQ(Hooks.mFrees, 0u);
			Mapping.mbCommit = false;
			Mapping.mBufferOffset = Tile;
			ASSERT_TRUE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
			mDevice->TrimGpuAllocator();
			EXPECT_EQ(Hooks.mFrees, 1u);
			Mapping.mBufferOffset = 0;
			ASSERT_TRUE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
			mDevice->TrimGpuAllocator();
			EXPECT_EQ(Hooks.mFrees, 2u);
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, AcceptedSparseRemapRetainsBothGenerationsUntilRetirement)
		{
			if (!mDevice->GetCapabilities().mResidency.mbReservedBuffers)
			{
				GTEST_SKIP() << "Vulkan sparse buffers are unavailable.";
			}
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 1024 * 1024;
			Desc.mbTiled = true;
			auto Buffer = mDevice->CreateBuffer(Desc);
			ASSERT_TRUE(Buffer);
			const auto Requirements = mDevice->GetBufferMemoryRequirements(Buffer.mValue);
			ASSERT_TRUE(Requirements);
			FArdaRHIHeapDesc HeapDesc;
			HeapDesc.mCapacity = Requirements.mValue.mAlignment;
			HeapDesc.mMemoryTypeBits = Requirements.mValue.mMemoryTypeBits;
			FArdaVulkanAuditHooks Hooks;
			auto First = mDevice->CreateHeap(HeapDesc);
			auto Second = mDevice->CreateHeap(HeapDesc);
			auto Third = mDevice->CreateHeap(HeapDesc);
			ASSERT_TRUE(First);
			ASSERT_TRUE(Second);
			ASSERT_TRUE(Third);
			FArdaRHIBufferTileMapping Mapping;
			Mapping.mByteSize = HeapDesc.mCapacity;
			Mapping.mHeap = First.mValue;
			ASSERT_TRUE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
			First.mValue = {};
			Mapping.mHeap = Second.mValue;
			Hooks.mbCaptureNativeAllocations = true;
			Hooks.mbFailWaitBeforeCompletion = true;
			EXPECT_FALSE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
			Hooks.mbFailWaitBeforeCompletion = false;
			ASSERT_EQ(Hooks.mCreatedFences.size(), 1u);
			Hooks.mWithheldFence = Hooks.mCreatedFences.front();
			// Establish native ordering without providing retirement proof to the provider.
			ASSERT_EQ(Hooks.mOriginal
			              .vkWaitForFences(Hooks.mNativeDevice, 1, Hooks.mCreatedFences.data(), VK_TRUE, UINT64_MAX),
			    VK_SUCCESS);
			Second.mValue = {};
			Mapping.mHeap = Third.mValue;
			// A second sparse-capable queue may retire its bind while the first wait lacks proof.
			auto Status = mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Compute);
			if (Status.mCode == EArdaRHIResult::Unsupported)
			{
				Status = mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Graphics);
			}
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			Mapping.mHeap = {};
			mDevice->TrimGpuAllocator();
			EXPECT_EQ(Hooks.mFrees, 0u);
			Hooks.mWithheldFence = VK_NULL_HANDLE;
			ASSERT_TRUE(mDevice->WaitForIdle());
			mDevice->RunGarbageCollection();
			mDevice->TrimGpuAllocator();
			EXPECT_EQ(Hooks.mFrees, 2u);
			// Reject overlapping native bind inputs before any submission or ownership publication.
			Mapping.mHeap = Third.mValue;
			const uint32_t FencesBefore = Hooks.mDestroyedFences;
			EXPECT_EQ(
			    mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping, Mapping}, EArdaRHIQueueType::Graphics).mCode,
			    EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(Hooks.mDestroyedFences, FencesBefore);
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, SparseImageModesRequireFullUnbindBeforeSwitching)
		{
			if (!mDevice->GetCapabilities().mResidency.mbReservedTexture2D)
			{
				GTEST_SKIP() << "Vulkan sparse images are unavailable.";
			}
			FArdaRHITextureDesc Desc;
			Desc.mWidth = Desc.mHeight = 512;
			Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
			Desc.mUsage = EArdaRHITextureUsage::ShaderResource;
			Desc.mbTiled = true;
			auto Texture = mDevice->CreateTexture(Desc);
			ASSERT_TRUE(Texture);
			const auto Tiling = mDevice->GetTextureTiling(Texture.mValue);
			ASSERT_TRUE(Tiling);
			if (!Tiling.mValue.mPackedMips.mStandardMipCount)
			{
				GTEST_SKIP() << "This sparse image exposes only an opaque mip tail.";
			}
			const auto Requirements = mDevice->GetTextureMemoryRequirements(Texture.mValue);
			ASSERT_TRUE(Requirements);
			FArdaRHIHeapDesc HeapDesc;
			HeapDesc.mCapacity = Requirements.mValue.mAlignment;
			HeapDesc.mMemoryTypeBits = Requirements.mValue.mMemoryTypeBits;
			auto Heap = mDevice->CreateHeap(HeapDesc);
			ASSERT_TRUE(Heap);
			FArdaRHITextureTileMapping Mapping;
			Mapping.mCoordinates = {{0, 0, 0, 0, 0}};
			Mapping.mRegions = {{1, 1, 1, 1}};
			Mapping.mByteOffsets = {0};
			Mapping.mHeap = Heap.mValue;
			ASSERT_TRUE(mDevice->UpdateTextureTileMappings(Texture.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
			FArdaVulkanAuditHooks Hooks;
			const FArdaRHIResourceRef Resource(Texture.mValue.Get());
			EXPECT_EQ(mDevice->CommitReservedResource(Resource, HeapDesc.mCapacity, EArdaRHIQueueType::Graphics).mCode,
			    EArdaRHIResult::Unsupported);
			EXPECT_EQ(Hooks.mDestroyedFences, 0u);
			ASSERT_TRUE(mDevice->CommitReservedResource(Resource, 0, EArdaRHIQueueType::Graphics));
			ASSERT_TRUE(mDevice->CommitReservedResource(Resource, HeapDesc.mCapacity, EArdaRHIQueueType::Graphics));
			const auto FencesBefore = Hooks.mDestroyedFences;
			EXPECT_EQ(mDevice->UpdateTextureTileMappings(Texture.mValue, {Mapping}, EArdaRHIQueueType::Graphics).mCode,
			    EArdaRHIResult::Unsupported);
			EXPECT_EQ(Hooks.mDestroyedFences, FencesBefore);
			ASSERT_TRUE(mDevice->CommitReservedResource(Resource, 0, EArdaRHIQueueType::Graphics));
			ASSERT_TRUE(mDevice->UpdateTextureTileMappings(Texture.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
			Mapping.mHeap = {};
			ASSERT_TRUE(mDevice->UpdateTextureTileMappings(Texture.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, ReportsNativeFormatAndDeviceLimits)
		{
			const auto& Caps = mDevice->GetCapabilities();
			EXPECT_GT(Caps.mLimits.mMaxTexture2D, 0u);
			EXPECT_GT(Caps.mLimits.mMaxComputeWorkGroupInvocations, 0u);
			EXPECT_GT(Caps.mLimits.mMaxBufferSize, 0u);
			const auto Color = mDevice->QueryFormatSupport(EArdaRHIFormat::RGBA8UNorm);
			EXPECT_EQ(Color.mNativeFormat, static_cast<uint64_t>(VK_FORMAT_R8G8B8A8_UNORM));
			EXPECT_TRUE(Color.mbTexture2D);
			EXPECT_TRUE(Color.mbShaderResource);
			EXPECT_TRUE(Color.mbColorAttachment);
			EXPECT_TRUE(Color.mSampleCounts & 1u);
			const auto Depth = mDevice->QueryFormatSupport(EArdaRHIFormat::D32);
			EXPECT_TRUE(Depth.mbDepthStencilAttachment);
			EXPECT_FALSE(Depth.mbColorAttachment);
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, ConcurrentSubmissionsSignalIncreasingTimelineValues)
		{
			constexpr uint32_t ThreadCount = 4;
			constexpr uint32_t CommandsPerThread = 32;
			std::vector<FArdaRHICommandListRef> Commands;
			for (uint32_t Index = 0; Index < ThreadCount * CommandsPerThread; ++Index)
			{
				auto Created = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Created);
				ASSERT_TRUE(Created.mValue->Open());
				ASSERT_TRUE(Created.mValue->Close());
				Commands.push_back(Created.mValue);
			}
			FArdaVulkanAuditHooks Hooks;
			std::atomic<bool> bStart{false};
			std::atomic<uint32_t> Failures{0};
			std::vector<std::thread> Threads;
			for (uint32_t ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
			{
				Threads.emplace_back(
				    [&, ThreadIndex]
				    {
					    while (!bStart.load(std::memory_order_acquire))
					    {
						    std::this_thread::yield();
					    }
					    for (uint32_t Index = 0; Index < CommandsPerThread; ++Index)
					    {
						    if (!mDevice->ExecuteCommandList(Commands[ThreadIndex * CommandsPerThread + Index]))
						    {
							    ++Failures;
						    }
					    }
				    });
			}
			bStart.store(true, std::memory_order_release);
			for (auto& Thread : Threads)
			{
				Thread.join();
			}
			EXPECT_EQ(Failures.load(), 0u);
			ASSERT_EQ(Hooks.mTimelineValues.size(), Commands.size());
			for (size_t Index = 1; Index < Hooks.mTimelineValues.size(); ++Index)
			{
				EXPECT_LT(Hooks.mTimelineValues[Index - 1], Hooks.mTimelineValues[Index]);
			}
			ASSERT_TRUE(mDevice->WaitForIdle());
		}

		TEST_F(FArdaVulkanLifetimeAuditTest, ShaderTableRecommitRetainsRecordedStorageAndSurvivesAllocationFailure)
		{
			if (!mDevice->GetCapabilities().mRayTracing.mbPipelineShaders)
			{
				GTEST_SKIP() << "Vulkan ray-tracing pipelines are unavailable.";
			}
			const std::string Path = std::string(ARDA_BACKEND_TEST_SHADER_DIR) + "/ArdaRayTracingTest" +
			    GetShaderArtifactExtension("native-vulkan");
			std::ifstream Stream(Path, std::ios::binary);
			ASSERT_TRUE(Stream.is_open());
			const std::vector<uint8_t> Bytecode{std::istreambuf_iterator<char>(Stream),
			    std::istreambuf_iterator<char>()};
			ASSERT_FALSE(Bytecode.empty());
			FArdaRHIShaderDesc ShaderDesc;
			ShaderDesc.mStage = EArdaRHIShaderStage::RayGeneration;
			ShaderDesc.mBytecode = Bytecode.data();
			ShaderDesc.mBytecodeSize = Bytecode.size();
			ShaderDesc.mEntryPoint = "RayGen";
			const auto Shader = mDevice->CreateShader(ShaderDesc);
			ASSERT_TRUE(Shader);
			FArdaRHIBindingLayoutDesc LayoutDesc;
			LayoutDesc.mVisibility = EArdaRHIShaderStage::AllRayTracing;
			LayoutDesc.mItems.push_back({0, 1, EArdaRHIBindingType::StructuredBufferUAV});
			const auto Layout = mDevice->CreateBindingLayout(LayoutDesc);
			ASSERT_TRUE(Layout);
			FArdaRHIRayTracingPipelineDesc PipelineDesc;
			PipelineDesc.mShaders.push_back({"RayGen", Shader.mValue, {}});
			PipelineDesc.mGlobalBindingLayouts.push_back(Layout.mValue);
			PipelineDesc.mMaxPayloadSize = sizeof(uint32_t);
			PipelineDesc.mMaxRecursionDepth = 1;
			const auto Pipeline = mDevice->CreateRayTracingPipeline(PipelineDesc);
			ASSERT_TRUE(Pipeline);
			FArdaRHIBufferDesc OutputDesc;
			OutputDesc.mByteSize = OutputDesc.mStructureStride = sizeof(uint32_t);
			OutputDesc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
			const auto Output = mDevice->CreateBuffer(OutputDesc);
			ASSERT_TRUE(Output);
			FArdaRHIBindingSetDesc SetDesc;
			SetDesc.mLayout = Layout.mValue;
			FArdaRHIBindingItem Item;
			Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
			Item.mResource = FArdaRHIResourceRef(Output.mValue.Get());
			SetDesc.mItems.push_back(Item);
			const auto Set = mDevice->CreateBindingSet(SetDesc);
			ASSERT_TRUE(Set);
			FArdaRHIShaderTableDesc TableDesc;
			TableDesc.mMaxEntries = 1;
			const auto Table = mDevice->CreateShaderTable(Pipeline.mValue, TableDesc);
			ASSERT_TRUE(Table);
			ASSERT_TRUE(mDevice->SetShaderTableRayGeneration(Table.mValue, "RayGen"));
			const auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			ASSERT_TRUE(Commands.mValue->SetBufferState(*Output.mValue, EArdaRHIResourceState::UnorderedAccess));
			FArdaRHIRayTracingState State;
			State.mShaderTable = Table.mValue;
			State.mBindings.push_back(Set.mValue);
			ASSERT_TRUE(Commands.mValue->SetRayTracingState(State));

			FArdaVulkanAuditHooks Hooks;
			// Recommitting between binding and dispatch preserves the bound generation.
			ASSERT_TRUE(mDevice->CommitShaderTable(Table.mValue));
			EXPECT_EQ(Hooks.mDestroyedBuffers.size(), 0u);
			ASSERT_TRUE(Commands.mValue->DispatchRays(1, 1, 1));
			ASSERT_TRUE(mDevice->CommitShaderTable(Table.mValue));
			EXPECT_EQ(Hooks.mDestroyedBuffers.size(), 1u);
			Hooks.mbFailCreateBuffer = true;
			EXPECT_EQ(mDevice->CommitShaderTable(Table.mValue).mCode, EArdaRHIResult::BackendFailure);
			Hooks.mbFailCreateBuffer = false;
			EXPECT_EQ(Hooks.mDestroyedBuffers.size(), 1u);

			// The command recorded before recommit still references live GPU memory.
			eastl::vector<uint8_t> Readback;
			ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Output.mValue, Readback));
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			ASSERT_EQ(Readback.size(), sizeof(uint32_t));
			uint32_t Value = 0;
			std::memcpy(&Value, Readback.data(), sizeof(Value));
			EXPECT_EQ(Value, 0xA11CEu);

			// Failed publication also leaves the last complete generation available for later work.
			ASSERT_TRUE(Commands.mValue->Reset());
			ASSERT_TRUE(Commands.mValue->SetRayTracingState(State));
			ASSERT_TRUE(Commands.mValue->DispatchRays(1, 1, 1));
			ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Output.mValue, Readback));
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			std::memcpy(&Value, Readback.data(), sizeof(Value));
			EXPECT_EQ(Value, 0xA11CEu);
		}
	}
}
#endif
