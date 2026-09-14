#include "ArdaTestBackend.h"
#include "ArdaBackend.h"

#include <gtest/gtest.h>

#include <cstring>

namespace
{
	using namespace arda;

	class FArdaGpuAllocatorGpuTest : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mbEnableValidation = ArdaTestValidationEnabled;
			Configuration.mMessageCallback = &mDiagnostics;
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
			mDevice->TrimGpuAllocator();
		}

		void TearDown() override
		{
			const bool bMissingValidation = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice->RunGarbageCollection();
				mDevice->TrimGpuAllocator();
			}
			mDevice.Reset();
			// Keep the callback alive through native resource destruction and device shutdown.
			ShutdownBackend();
			if (!bMissingValidation)
			{
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}

		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	FArdaRHIBufferDesc MakeAllocatorBufferDesc()
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 1024;
		Desc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess;
		Desc.mInitialState = EArdaRHIResourceState::Common;
		Desc.mDebugName = "GPU allocator buffer";
		return Desc;
	}

	TEST_P(FArdaGpuAllocatorGpuTest, ExplicitHeapAndPlacedBufferCreationsPlateau)
	{
		auto Desc = MakeAllocatorBufferDesc();
		Desc.mbVirtual = true;
		const auto Requirements = mDevice->QueryBufferMemoryRequirements(Desc);
		ASSERT_TRUE(Requirements) << Requirements.mStatus.mMessage.c_str();
		FArdaRHIHeapDesc HeapDesc;
		HeapDesc.mCapacity = Requirements.mValue.mSize;
		HeapDesc.mMemoryTypeBits = Requirements.mValue.mMemoryTypeBits;
		HeapDesc.mDebugName = "GPU allocator shared heap";
		const auto Before = mDevice->GetGpuAllocatorStats();
		FArdaRHIMemoryAllocationInfo Allocation;

		for (uint32_t Iteration = 0; Iteration < 4; ++Iteration)
		{
			SCOPED_TRACE(Iteration);
			{
				auto Heap = mDevice->CreateHeap(HeapDesc);
				ASSERT_TRUE(Heap) << Heap.mStatus.mMessage.c_str();
				auto Buffer = mDevice->CreatePlacedBuffer(Desc, Heap.mValue, 0);
				ASSERT_TRUE(Buffer) << Buffer.mStatus.mMessage.c_str();
				const auto Current = Buffer.mValue->GetMemoryAllocationInfo();
				ASSERT_TRUE(Current.mbKnown);
				ASSERT_NE(Current.mIdentity, nullptr);
				EXPECT_EQ(Current, Heap.mValue->GetMemoryAllocationInfo());
				if (Iteration == 0)
				{
					Allocation = Current;
				}
				else
				{
					EXPECT_EQ(Current, Allocation);
				}
			}
			mDevice->RunGarbageCollection();
			const auto After = mDevice->GetGpuAllocatorStats();
			EXPECT_EQ(After.mHeapCreations, Before.mHeapCreations + 1);
			EXPECT_EQ(After.mBufferCreations, Before.mBufferCreations + 1);
			EXPECT_EQ(After.mHeapCacheHits, Before.mHeapCacheHits + Iteration);
			EXPECT_EQ(After.mBufferCacheHits, Before.mBufferCacheHits + Iteration);
			EXPECT_GE(After.mCachedHeapBytes, Allocation.mByteSize);
		}
	}

	TEST_P(FArdaGpuAllocatorGpuTest, DirectCommonBufferCreationsPlateau)
	{
		const auto Desc = MakeAllocatorBufferDesc();
		const auto Before = mDevice->GetGpuAllocatorStats();
		FArdaRHIMemoryAllocationInfo Allocation;
		for (uint32_t Iteration = 0; Iteration < 4; ++Iteration)
		{
			SCOPED_TRACE(Iteration);
			{
				auto Buffer = mDevice->CreateBuffer(Desc);
				ASSERT_TRUE(Buffer) << Buffer.mStatus.mMessage.c_str();
				const auto Current = Buffer.mValue->GetMemoryAllocationInfo();
				ASSERT_TRUE(Current.mbKnown);
				ASSERT_NE(Current.mIdentity, nullptr);
				if (Iteration == 0)
				{
					Allocation = Current;
				}
				else
				{
					EXPECT_EQ(Current, Allocation);
				}
			}
			mDevice->RunGarbageCollection();
			const auto After = mDevice->GetGpuAllocatorStats();
			EXPECT_EQ(After.mBufferCreations, Before.mBufferCreations + 1);
			EXPECT_EQ(After.mBufferCacheHits, Before.mBufferCacheHits + Iteration);
		}
	}

	TEST_P(FArdaGpuAllocatorGpuTest, WrittenAndReadBackBufferReusesStorageWithConsistentCommonState)
	{
		const auto Desc = MakeAllocatorBufferDesc();
		const auto Before = mDevice->GetGpuAllocatorStats();
		FArdaRHIMemoryAllocationInfo Allocation;
		uint64_t WarmBufferCreations = 0;
		for (uint32_t Iteration = 0; Iteration < 3; ++Iteration)
		{
			SCOPED_TRACE(Iteration);
			{
				auto Buffer = mDevice->CreateBuffer(Desc);
				ASSERT_TRUE(Buffer) << Buffer.mStatus.mMessage.c_str();
				if (Iteration == 0)
				{
					Allocation = Buffer.mValue->GetMemoryAllocationInfo();
				}
				else
				{
					EXPECT_EQ(Buffer.mValue->GetMemoryAllocationInfo(), Allocation);
				}
				auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Commands);
				ASSERT_TRUE(Commands.mValue->Open());
				const auto Initial = Commands.mValue->QueryBufferState(*Buffer.mValue);
				ASSERT_TRUE(Initial) << Initial.mStatus.mMessage.c_str();
				EXPECT_TRUE(Initial.mValue.IsConsistent());
				EXPECT_EQ(Initial.mValue.mFacadeState, EArdaRHIResourceState::Common);
				const uint32_t Pattern = 0x1234ab00u + Iteration;
				ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, &Pattern, sizeof(Pattern)));
				eastl::vector<uint8_t> Readback;
				ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Readback, 0, sizeof(Pattern)));
				ASSERT_TRUE(Commands.mValue->SetBufferState(*Buffer.mValue, EArdaRHIResourceState::Common));
				ASSERT_TRUE(Commands.mValue->Close());
				const auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
				ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
				ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
				ASSERT_EQ(Readback.size(), sizeof(Pattern));
				uint32_t Actual = 0;
				std::memcpy(&Actual, Readback.data(), sizeof(Actual));
				EXPECT_EQ(Actual, Pattern);
			}
			mDevice->RunGarbageCollection();
			const auto After = mDevice->GetGpuAllocatorStats();
			if (Iteration == 0)
			{
				// Upload/readback storage may also enter the shared allocator during warmup.
				WarmBufferCreations = After.mBufferCreations;
			}
			EXPECT_EQ(After.mBufferCreations, WarmBufferCreations);
			EXPECT_GE(After.mBufferCacheHits, Before.mBufferCacheHits + Iteration);
		}
	}

	TEST_P(FArdaGpuAllocatorGpuTest, UploadAndReadbackCreationsPlateauOnEverySupportedQueue)
	{
		const EArdaRHIQueueType Queues[] = {EArdaRHIQueueType::Graphics,
		    EArdaRHIQueueType::Compute,
		    EArdaRHIQueueType::Copy};
		for (const auto Queue : Queues)
		{
			if (!mDevice->GetCapabilities().IsQueueSupported(Queue))
			{
				continue;
			}
			SCOPED_TRACE(static_cast<uint32_t>(Queue));
			mDevice->TrimGpuAllocator();
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = sizeof(uint32_t);
			Desc.mInitialState = EArdaRHIResourceState::Common;
			Desc.mDebugName = "Queue allocator upload destination";
			// A fresh destination acquires this queue's family on first use and stays alive throughout
			// the loop, isolating the hidden upload/readback allocations being tested.
			auto Buffer = mDevice->CreateBuffer(Desc);
			ASSERT_TRUE(Buffer) << Buffer.mStatus.mMessage.c_str();
			const auto Before = mDevice->GetGpuAllocatorStats();
			uint64_t WarmBufferCreations = 0;
			for (uint32_t Iteration = 0; Iteration < 4; ++Iteration)
			{
				SCOPED_TRACE(Iteration);
				{
					auto Commands = mDevice->CreateCommandList(Queue);
					ASSERT_TRUE(Commands);
					ASSERT_TRUE(Commands.mValue->Open());
					const uint32_t Pattern = 0xabc00000u + static_cast<uint32_t>(Queue) * 16u + Iteration;
					ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, &Pattern, sizeof(Pattern)));
					eastl::vector<uint8_t> Readback;
					ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Readback, 0, sizeof(Pattern)));
					ASSERT_TRUE(Commands.mValue->SetBufferState(*Buffer.mValue, EArdaRHIResourceState::Common));
					ASSERT_TRUE(Commands.mValue->Close());
					const auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
					ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
					ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
					ASSERT_EQ(Readback.size(), sizeof(Pattern));
					uint32_t Actual = 0;
					std::memcpy(&Actual, Readback.data(), sizeof(Actual));
					EXPECT_EQ(Actual, Pattern);
				}
				mDevice->RunGarbageCollection();
				const auto After = mDevice->GetGpuAllocatorStats();
				if (Iteration == 0)
				{
					WarmBufferCreations = After.mBufferCreations;
					EXPECT_EQ(WarmBufferCreations, Before.mBufferCreations + 2);
				}
				EXPECT_EQ(After.mBufferCreations, WarmBufferCreations);
				EXPECT_EQ(After.mBufferCacheHits, Before.mBufferCacheHits + Iteration * 2u);
			}
		}
	}

	TEST_P(FArdaGpuAllocatorGpuTest, ChangedNativeStateDeclinesReuseInsteadOfResettingBookkeeping)
	{
		const auto Desc = MakeAllocatorBufferDesc();
		{
			auto Buffer = mDevice->CreateBuffer(Desc);
			ASSERT_TRUE(Buffer);
			auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			ASSERT_TRUE(Commands.mValue->SetBufferState(*Buffer.mValue, EArdaRHIResourceState::CopyDest));
			ASSERT_TRUE(Commands.mValue->Close());
			const auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
		}
		mDevice->RunGarbageCollection();
		const auto Before = mDevice->GetGpuAllocatorStats();
		auto Buffer = mDevice->CreateBuffer(Desc);
		ASSERT_TRUE(Buffer);
		const auto After = mDevice->GetGpuAllocatorStats();
		EXPECT_EQ(After.mBufferCreations, Before.mBufferCreations + 1);
		EXPECT_EQ(After.mBufferCacheHits, Before.mBufferCacheHits);
		auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		const auto Initial = Commands.mValue->QueryBufferState(*Buffer.mValue);
		ASSERT_TRUE(Initial);
		EXPECT_TRUE(Initial.mValue.IsConsistent());
		EXPECT_EQ(Initial.mValue.mFacadeState, EArdaRHIResourceState::Common);
		ASSERT_TRUE(Commands.mValue->Close());
	}

	TEST_P(FArdaGpuAllocatorGpuTest, SubmittedCommandOwnershipPreventsEarlyBufferReuse)
	{
		const auto Desc = MakeAllocatorBufferDesc();
		auto Buffer = mDevice->CreateBuffer(Desc);
		ASSERT_TRUE(Buffer);
		const auto Allocation = Buffer.mValue->GetMemoryAllocationInfo();
		auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		const uint32_t Pattern = 0x4321dcba;
		ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, &Pattern, sizeof(Pattern)));
		ASSERT_TRUE(Commands.mValue->SetBufferState(*Buffer.mValue, EArdaRHIResourceState::Common));
		ASSERT_TRUE(Commands.mValue->Close());
		const auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
		ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
		Buffer.mValue.Reset();
		const auto Before = mDevice->GetGpuAllocatorStats();
		// Keep the submitted recording alive, making retention independent of GPU scheduling speed.
		auto Concurrent = mDevice->CreateBuffer(Desc);
		ASSERT_TRUE(Concurrent);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mBufferCreations, Before.mBufferCreations + 1);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mBufferCacheHits, Before.mBufferCacheHits);
		ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
		Commands.mValue.Reset();
		mDevice->RunGarbageCollection();
		const auto Retired = mDevice->GetGpuAllocatorStats();
		auto Reused = mDevice->CreateBuffer(Desc);
		ASSERT_TRUE(Reused);
		EXPECT_EQ(Reused.mValue->GetMemoryAllocationInfo(), Allocation);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mBufferCreations, Retired.mBufferCreations);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mBufferCacheHits, Retired.mBufferCacheHits + 1);
	}

	TEST_P(FArdaGpuAllocatorGpuTest, TextureRestoredToCommonReusesStorageAndAllSubresourceStates)
	{
		FArdaRHITextureDesc Desc;
		Desc.mWidth = 16;
		Desc.mHeight = 16;
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mArraySize = 2;
		Desc.mMipLevels = 2;
		Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		Desc.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
		Desc.mInitialState = EArdaRHIResourceState::Common;
		Desc.mDebugName = "GPU allocator texture";
		const auto Before = mDevice->GetGpuAllocatorStats();
		FArdaRHIMemoryAllocationInfo Allocation;
		for (uint32_t Iteration = 0; Iteration < 3; ++Iteration)
		{
			SCOPED_TRACE(Iteration);
			{
				auto Texture = mDevice->CreateTexture(Desc);
				ASSERT_TRUE(Texture) << Texture.mStatus.mMessage.c_str();
				if (Iteration == 0)
				{
					Allocation = Texture.mValue->GetMemoryAllocationInfo();
					ASSERT_TRUE(Allocation.mbKnown);
				}
				else
				{
					EXPECT_EQ(Texture.mValue->GetMemoryAllocationInfo(), Allocation);
				}
				auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Commands);
				ASSERT_TRUE(Commands.mValue->Open());
				const auto Initial = Commands.mValue->QueryTextureState(*Texture.mValue, {});
				ASSERT_TRUE(Initial) << Initial.mStatus.mMessage.c_str();
				EXPECT_TRUE(Initial.mValue.IsConsistent());
				EXPECT_EQ(Initial.mValue.mFacadeState, EArdaRHIResourceState::Common);
				ASSERT_TRUE(
				    Commands.mValue->SetTextureState(*Texture.mValue, {}, EArdaRHIResourceState::UnorderedAccess));
				ASSERT_TRUE(Commands.mValue->SetTextureState(*Texture.mValue, {}, EArdaRHIResourceState::Common));
				ASSERT_TRUE(Commands.mValue->Close());
				const auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
				ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
				ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
			}
			mDevice->RunGarbageCollection();
			const auto After = mDevice->GetGpuAllocatorStats();
			EXPECT_EQ(After.mTextureCreations, Before.mTextureCreations + 1);
			EXPECT_EQ(After.mTextureCacheHits, Before.mTextureCacheHits + Iteration);
		}
	}

	const char* const NativeBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
	    "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
	    "native-vulkan",
#endif
	};
	INSTANTIATE_TEST_SUITE_P(NativeProviders, FArdaGpuAllocatorGpuTest, testing::ValuesIn(NativeBackends));
}
