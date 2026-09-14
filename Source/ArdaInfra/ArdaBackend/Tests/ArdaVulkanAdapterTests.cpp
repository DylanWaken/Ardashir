#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"

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
