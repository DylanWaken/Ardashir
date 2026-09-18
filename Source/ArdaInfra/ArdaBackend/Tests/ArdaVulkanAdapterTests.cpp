#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"

#include <gtest/gtest.h>

#include <cstring>

#if defined(ARDA_TEST_NATIVE_VULKAN)
namespace arda
{
	namespace
	{
		class FArdaVulkanAdapterTest : public testing::Test
		{
		protected:
			void SetUp() override
			{
				ShutdownBackend();
				mConfiguration = MakeArdaTestBackendConfiguration();
				mConfiguration.mBackendName = "native-vulkan";
				mConfiguration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
				mConfiguration.mMessageCallback = &mDiagnostics;
				ASSERT_TRUE(ConfigureBackend(mConfiguration));
				ARDA_REQUIRE_BACKEND();
				mDevice = GetDevice();
				ASSERT_TRUE(mDevice);
			}

			void TearDown() override
			{
				mDevice = {};
				ShutdownBackend();
				(void)ConfigureBackend(MakeArdaTestBackendConfiguration());
			}

			FArdaBackendConfiguration mConfiguration;
			FArdaRHIDeviceRef mDevice;
			FArdaTestDiagnosticCallback mDiagnostics;
		};

		TEST_F(FArdaVulkanAdapterTest, EnumerationPreservesLiveDeviceAndAdapterIdentity)
		{
			const auto Devices = GetDevices();
			ASSERT_EQ(Devices.size(), 1u);
			const auto ActualAdapter = Devices.front().mAdapter;
			ASSERT_EQ(ActualAdapter.mId.mBackendName, "native-vulkan");
			ASSERT_FALSE(ActualAdapter.mId.mValue.empty());

			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = sizeof(uint32_t);
			const auto Buffer = mDevice->CreateBuffer(Desc);
			ASSERT_TRUE(Buffer);
			const auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());

			const auto Adapters = EnumerateAdapters("native-vulkan");
			ASSERT_TRUE(Adapters) << Adapters.mStatus.mMessage.c_str();
			bool bFoundActualAdapter = false;
			for (const auto& Adapter : Adapters.mValue)
			{
				bFoundActualAdapter |= Adapter.mId == ActualAdapter.mId;
				EXPECT_EQ(Adapter.mId.mBackendName, "native-vulkan");
				EXPECT_FALSE(Adapter.mId.mValue.empty());
				EXPECT_FALSE(Adapter.mName.empty());
			}
			EXPECT_TRUE(bFoundActualAdapter);
			EXPECT_EQ(GetDevice(), mDevice);

			// The live device must still record, submit and read back after discovery destroys its instance.
			constexpr uint32_t Expected = 0x1234abcd;
			eastl::vector<uint8_t> Bytes;
			ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, &Expected, sizeof(Expected)));
			ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
			ASSERT_TRUE(Commands.mValue->Close());
			const auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			ASSERT_EQ(Bytes.size(), sizeof(Expected));
			EXPECT_EQ(std::memcmp(Bytes.data(), &Expected, sizeof(Expected)), 0);
		}

		TEST_F(FArdaVulkanAdapterTest, PrerecordedTextureConsumerPreservesProducerContentsAndNativeState)
		{
			FArdaRHITextureDesc Desc;
			Desc.mWidth = Desc.mHeight = 4;
			Desc.mMipLevels = 2;
			Desc.mFormat = EArdaRHIFormat::R32Float;
			Desc.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
			const auto Texture = mDevice->CreateTexture(Desc);
			ASSERT_TRUE(Texture);
			FArdaRHIStagingTextureDesc StagingDesc;
			StagingDesc.mTexture = Desc;
			StagingDesc.mCpuAccess = EArdaRHICpuAccess::Read;
			const auto Staging = mDevice->CreateStagingTexture(StagingDesc);
			ASSERT_TRUE(Staging);
			const FArdaRHITextureSubresourceRange Mip0{0, 1, 0, 1};
			const FArdaRHITextureSubresourceRange Mip1{1, 1, 0, 1};
			const auto Producer = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			const auto Consumer = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Producer);
			ASSERT_TRUE(Consumer);
			ASSERT_TRUE(Producer.mValue->Open());
			ASSERT_TRUE(Producer.mValue->BeginTrackingTextureState(*Texture.mValue, Mip0, Desc.mInitialState));
			const auto Initial = Producer.mValue->QueryTextureState(*Texture.mValue, Mip0);
			ASSERT_TRUE(Initial);
			ASSERT_TRUE(Producer.mValue->ClearTexture(*Texture.mValue, Mip0, {0.625f, 0.0f, 0.0f, 0.0f}));
			ASSERT_TRUE(
			    Producer.mValue->SetTextureState(*Texture.mValue, Mip0, EArdaRHIResourceState::UnorderedAccess));
			const auto Produced = Producer.mValue->QueryTextureState(*Texture.mValue, Mip0);
			ASSERT_TRUE(Produced);
			ASSERT_TRUE(Producer.mValue->Close());

			// Inductor records consumers before submitting their producers. The image's
			// global snapshot is still undefined, but the declared start state is authoritative.
			ASSERT_TRUE(Consumer.mValue->Open());
			ASSERT_TRUE(Consumer.mValue->BeginTrackingTextureState(*Texture.mValue,
			    Mip0,
			    EArdaRHIResourceState::UnorderedAccess));
			const auto Consumed = Consumer.mValue->QueryTextureState(*Texture.mValue, Mip0);
			const auto Untouched = Consumer.mValue->QueryTextureState(*Texture.mValue, Mip1);
			ASSERT_TRUE(Consumed);
			ASSERT_TRUE(Untouched);
			EXPECT_EQ(Consumed.mValue.mNative.mPrimaryState, Produced.mValue.mNative.mPrimaryState);
			EXPECT_EQ(Consumed.mValue.mNative.mPipelineStageMask, Produced.mValue.mNative.mPipelineStageMask);
			EXPECT_EQ(Consumed.mValue.mNative.mAccessMask, Produced.mValue.mNative.mAccessMask);
			EXPECT_EQ(Untouched.mValue.mNative.mPrimaryState, Initial.mValue.mNative.mPrimaryState);
			ASSERT_TRUE(Consumer.mValue->SetTextureState(*Texture.mValue, Mip0, EArdaRHIResourceState::ShaderResource));
			ASSERT_TRUE(Consumer.mValue->CopyTextureToStaging(*Staging.mValue, {}, *Texture.mValue, {}));
			ASSERT_TRUE(Consumer.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Producer.mValue));
			ASSERT_TRUE(mDevice->ExecuteCommandList(Consumer.mValue));
			ASSERT_TRUE(mDevice->WaitForIdle());
			const auto Mapped = mDevice->MapStagingTexture(Staging.mValue, {}, EArdaRHICpuAccess::Read);
			ASSERT_TRUE(Mapped);
			for (uint32_t Y = 0; Y < Desc.mHeight; ++Y)
			{
				for (uint32_t X = 0; X < Desc.mWidth; ++X)
				{
					float Value = 0.0f;
					std::memcpy(&Value,
					    static_cast<const uint8_t*>(Mapped.mValue.mData) + Y * Mapped.mValue.mRowPitch +
					        X * sizeof(float),
					    sizeof(Value));
					EXPECT_FLOAT_EQ(Value, 0.625f);
				}
			}
			ASSERT_TRUE(mDevice->UnmapStagingTexture(Staging.mValue));
		}

		TEST_F(FArdaVulkanAdapterTest, PrerecordedTextureAcquirePreservesCommonLayoutAndContents)
		{
			for (const auto Queue : {EArdaRHIQueueType::Graphics, EArdaRHIQueueType::Compute})
			{
				SCOPED_TRACE(static_cast<uint32_t>(Queue));
				if (Queue == EArdaRHIQueueType::Compute && !mDevice->GetCapabilities().mQueues.mbCompute)
				{
					continue;
				}
				FArdaRHITextureDesc Desc;
				// Use distinct extents so the second case cannot reuse the first case's initialized image.
				Desc.mWidth = Desc.mHeight = Queue == EArdaRHIQueueType::Graphics ? 4 : 8;
				Desc.mFormat = EArdaRHIFormat::R32Float;
				Desc.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
				Desc.mInitialState = EArdaRHIResourceState::Common;
				const auto Texture = mDevice->CreateTexture(Desc);
				ASSERT_TRUE(Texture);
				FArdaRHIStagingTextureDesc StagingDesc;
				StagingDesc.mTexture = Desc;
				StagingDesc.mCpuAccess = EArdaRHICpuAccess::Read;
				const auto Staging = mDevice->CreateStagingTexture(StagingDesc);
				const auto Producer = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				const auto Consumer = mDevice->CreateCommandList(Queue);
				ASSERT_TRUE(Staging);
				ASSERT_TRUE(Producer);
				ASSERT_TRUE(Consumer);
				ASSERT_TRUE(Producer.mValue->Open());
				ASSERT_TRUE(Producer.mValue->ClearTexture(*Texture.mValue, {}, {0.375f, 0.0f, 0.0f, 0.0f}));
				ASSERT_TRUE(Producer.mValue->SetTextureState(*Texture.mValue, {}, EArdaRHIResourceState::Common));
				FArdaRHITextureTransitionDesc Transfer;
				Transfer.mStateBefore = Transfer.mStateAfter = EArdaRHIResourceState::Common;
				Transfer.mSourceQueue = EArdaRHIQueueType::Graphics;
				Transfer.mDestinationQueue = Queue;
				Transfer.mbQueueOwnershipTransfer = true;
				Transfer.mFlags = EArdaRHITransitionFlags::BeginOnly;
				ASSERT_TRUE(Producer.mValue->TransitionTexture(*Texture.mValue, Transfer));
				const auto Released = Producer.mValue->QueryTextureState(*Texture.mValue, {});
				ASSERT_TRUE(Released);
				ASSERT_TRUE(Producer.mValue->Close());

				ASSERT_TRUE(Consumer.mValue->Open());
				ASSERT_TRUE(
				    Consumer.mValue->BeginTrackingTextureState(*Texture.mValue, {}, EArdaRHIResourceState::Common));
				const auto BeforeAcquire = Consumer.mValue->QueryTextureState(*Texture.mValue, {});
				ASSERT_TRUE(BeforeAcquire);
				EXPECT_NE(BeforeAcquire.mValue.mNative.mPrimaryState, Released.mValue.mNative.mPrimaryState);
				Transfer.mFlags = EArdaRHITransitionFlags::EndOnly;
				ASSERT_TRUE(Consumer.mValue->TransitionTexture(*Texture.mValue, Transfer));
				const auto Acquired = Consumer.mValue->QueryTextureState(*Texture.mValue, {});
				ASSERT_TRUE(Acquired);
				EXPECT_EQ(Acquired.mValue.mNative.mPrimaryState, Released.mValue.mNative.mPrimaryState);
				EXPECT_EQ(Acquired.mValue.mNative.mPipelineStageMask, Released.mValue.mNative.mPipelineStageMask);
				EXPECT_EQ(Acquired.mValue.mNative.mAccessMask, Released.mValue.mNative.mAccessMask);
				ASSERT_TRUE(Consumer.mValue->CopyTextureToStaging(*Staging.mValue, {}, *Texture.mValue, {}));
				ASSERT_TRUE(Consumer.mValue->Close());
				const auto Submitted = mDevice->ExecuteCommandList(Producer.mValue);
				ASSERT_TRUE(Submitted);
				ASSERT_TRUE(mDevice->QueueWait(Queue, EArdaRHIQueueType::Graphics, Submitted.mValue));
				ASSERT_TRUE(mDevice->ExecuteCommandList(Consumer.mValue));
				ASSERT_TRUE(mDevice->WaitForIdle());
				const auto Mapped = mDevice->MapStagingTexture(Staging.mValue, {}, EArdaRHICpuAccess::Read);
				ASSERT_TRUE(Mapped);
				for (uint32_t Y = 0; Y < Desc.mHeight; ++Y)
				{
					for (uint32_t X = 0; X < Desc.mWidth; ++X)
					{
						float Value = 0.0f;
						std::memcpy(&Value,
						    static_cast<const uint8_t*>(Mapped.mValue.mData) + Y * Mapped.mValue.mRowPitch +
						        X * sizeof(float),
						    sizeof(Value));
						EXPECT_FLOAT_EQ(Value, 0.375f);
					}
				}
				ASSERT_TRUE(mDevice->UnmapStagingTexture(Staging.mValue));
				// Cross-family acquires must not use UNDEFINED as newLayout, even on
				// drivers that happen to preserve contents after a discard transition.
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}

		TEST_F(FArdaVulkanAdapterTest, InitialTextureTrackingPreservesTheFirstLayoutTransition)
		{
			for (const auto InitialState : {EArdaRHIResourceState::Common, EArdaRHIResourceState::UnorderedAccess})
			{
				FArdaRHITextureDesc Desc;
				Desc.mWidth = Desc.mHeight = 4;
				Desc.mFormat = EArdaRHIFormat::R32Float;
				Desc.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
				Desc.mInitialState = InitialState;
				const auto Texture = mDevice->CreateTexture(Desc);
				const auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Texture);
				ASSERT_TRUE(Commands);
				ASSERT_TRUE(Commands.mValue->Open());
				const auto Initial = Commands.mValue->QueryTextureState(*Texture.mValue, {});
				ASSERT_TRUE(Initial);
				ASSERT_TRUE(Commands.mValue->BeginTrackingTextureState(*Texture.mValue, {}, InitialState));
				const auto Tracked = Commands.mValue->QueryTextureState(*Texture.mValue, {});
				ASSERT_TRUE(Tracked);
				EXPECT_EQ(Tracked.mValue.mNative.mPrimaryState, Initial.mValue.mNative.mPrimaryState);
				EXPECT_EQ(Tracked.mValue.mNative.mAccessMask, Initial.mValue.mNative.mAccessMask);
				ASSERT_TRUE(
				    Commands.mValue->BeginTrackingTextureState(*Texture.mValue, {}, EArdaRHIResourceState::Unknown));
				const auto Unknown = Commands.mValue->QueryTextureState(*Texture.mValue, {});
				ASSERT_TRUE(Unknown);
				EXPECT_EQ(Unknown.mValue.mNative.mPrimaryState, Initial.mValue.mNative.mPrimaryState);
				ASSERT_TRUE(Commands.mValue->BeginTrackingTextureState(*Texture.mValue, {}, InitialState));
				ASSERT_TRUE(
				    Commands.mValue->SetTextureState(*Texture.mValue, {}, EArdaRHIResourceState::UnorderedAccess));
				const auto Transitioned = Commands.mValue->QueryTextureState(*Texture.mValue, {});
				ASSERT_TRUE(Transitioned);
				EXPECT_NE(Transitioned.mValue.mNative.mPrimaryState, Initial.mValue.mNative.mPrimaryState);
				ASSERT_TRUE(Commands.mValue->Close());
				ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
				ASSERT_TRUE(mDevice->WaitForIdle());
			}
		}

		TEST_F(FArdaVulkanAdapterTest, ExplicitSelectionReportsTheRequestedAdapter)
		{
			FArdaAdapterId Requested;
			{
				const auto Devices = GetDevices();
				ASSERT_EQ(Devices.size(), 1u);
				Requested = Devices.front().mAdapter.mId;
			}
			mDevice = {};
			ShutdownBackend();
			mConfiguration.mAdapters = {Requested};
			ASSERT_TRUE(ConfigureBackend(mConfiguration));
			ARDA_REQUIRE_BACKEND();
			const auto Devices = GetDevices();
			ASSERT_EQ(Devices.size(), 1u);
			EXPECT_TRUE(Devices.front().mAdapter.mId == Requested);
		}

		TEST_F(FArdaVulkanAdapterTest, MissingRequestedAdapterDoesNotFallBack)
		{
			mDevice = {};
			ShutdownBackend();
			mConfiguration.mAdapters = {{"native-vulkan", "uuid:missing-adapter"}};
			ASSERT_TRUE(ConfigureBackend(mConfiguration));
			EXPECT_FALSE(InitializeBackend());
			EXPECT_EQ(GetBackendInitializeResult(), EArdaInitializeResult::Unavailable);
			EXPECT_FALSE(GetBackendError().empty());
			EXPECT_TRUE(GetDevices().empty());
		}

		TEST_F(FArdaVulkanAdapterTest, ModuleRejectsForeignAndIncompleteAdapterIdentities)
		{
			auto* Module = FindBackendModule("native-vulkan");
			ASSERT_NE(Module, nullptr);
			for (const FArdaAdapterId& Adapter : {FArdaAdapterId{"native-d3d12", "luid:123"},
			         FArdaAdapterId{"native-vulkan", ""},
			         FArdaAdapterId{"", "uuid:123"}})
			{
				const auto Result = Module->CreateDevice(mConfiguration, Adapter, nullptr, nullptr);
				EXPECT_EQ(Result.mResult, EArdaInitializeResult::Failure);
				EXPECT_FALSE(Result.mError.empty());
				EXPECT_FALSE(Result.mBackendRuntime);
				EXPECT_FALSE(Result.mProviderDevice);
			}
		}
	}
}
#endif
