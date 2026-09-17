#include "ArdaTestBackend.h"
#include "ArdaBackend.h"

#include <gtest/gtest.h>

#include <cstring>

namespace
{
	using namespace arda;

	class FArdaSubmissionTest : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			FArdaBackendConfiguration Configuration = arda::MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mbEnableValidation = arda::ArdaTestValidationEnabled;
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
		}

		void TearDown() override
		{
			mDevice = {};
			ShutdownBackend();
		}

		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(FArdaSubmissionTest, TokensRemainPollableAfterCompletionAndGarbageCollection)
	{
		for (const auto Queue : {EArdaRHIQueueType::Graphics, EArdaRHIQueueType::Compute, EArdaRHIQueueType::Copy})
		{
			if (!mDevice->GetCapabilities().IsQueueSupported(Queue))
			{
				continue;
			}
			const auto Commands = mDevice->CreateCommandList(Queue);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			ASSERT_TRUE(Commands.mValue->Close());
			const auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			EXPECT_TRUE(mDevice->PollSubmission(Submitted.mValue));
			ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
			mDevice->RunGarbageCollection();
			const auto Complete = mDevice->PollSubmission(Submitted.mValue);
			ASSERT_TRUE(Complete);
			EXPECT_TRUE(Complete.mValue);
			EXPECT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
		}
		EXPECT_EQ(mDevice->PollSubmission(UINT64_MAX).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(mDevice->WaitForSubmission(UINT64_MAX).mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(mDevice->PollSubmission((uint64_t(1) << 59) - 1).mStatus.mCode, EArdaRHIResult::InvalidArgument);
	}

	TEST_P(FArdaSubmissionTest, CommandListRetainsDeviceAfterBackendShutdown)
	{
		auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		auto* Device = mDevice.Get();
		mDevice = {};
		ShutdownBackend();
		ASSERT_EQ(Commands.mValue->GetDevice(), Device);

		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = sizeof(uint32_t);
		const auto Buffer = Device->CreateBuffer(Desc);
		ASSERT_TRUE(Buffer);
		ASSERT_TRUE(Commands.mValue->Open());
		constexpr uint32_t Expected = 0xBA51C123;
		ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, &Expected, sizeof(Expected)));
		eastl::vector<uint8_t> Readback;
		ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Readback));
		ASSERT_TRUE(Commands.mValue->Close());
		const auto Submitted = Device->ExecuteCommandList(Commands.mValue);
		ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
		ASSERT_TRUE(Device->WaitForSubmission(Submitted.mValue));
		ASSERT_EQ(Readback.size(), sizeof(Expected));
		EXPECT_EQ(std::memcmp(Readback.data(), &Expected, sizeof(Expected)), 0);
	}

	TEST_P(FArdaSubmissionTest, RejectsMisalignedIndirectArgumentsBeforeNativeRecording)
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 128;
		Desc.mUsage = EArdaRHIBufferUsage::Indirect;
		const auto Buffer = mDevice->CreateBuffer(Desc);
		const auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Buffer);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		ASSERT_TRUE(Commands.mValue->SetBufferState(*Buffer.mValue, EArdaRHIResourceState::IndirectArgument));
		for (uint64_t Offset : {1u, 2u, 3u})
		{
			EXPECT_EQ(Commands.mValue->DrawIndirect(*Buffer.mValue, Offset).mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(Commands.mValue->DrawIndexedIndirect(*Buffer.mValue, Offset).mCode,
			    EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(Commands.mValue->DispatchIndirect(*Buffer.mValue, Offset).mCode, EArdaRHIResult::InvalidArgument);
			if (mDevice->GetCapabilities().mRayTracing.mbIndirectDispatch)
			{
				EXPECT_EQ(Commands.mValue->DispatchRaysIndirect(*Buffer.mValue, Offset).mCode,
				    EArdaRHIResult::InvalidArgument);
			}
		}
		for (uint32_t Padding : {1u, 2u, 3u})
		{
			EXPECT_EQ(Commands.mValue->DrawIndirect(*Buffer.mValue, 0, 2, 16 + Padding).mCode,
			    EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(Commands.mValue->DrawIndexedIndirect(*Buffer.mValue, 0, 2, 20 + Padding).mCode,
			    EArdaRHIResult::InvalidArgument);
		}
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
	}

	TEST_P(FArdaSubmissionTest, RejectsOverflowingRayDispatchDimensions)
	{
		if (!mDevice->GetCapabilities().mRayTracing.mbPipelineShaders)
		{
			GTEST_SKIP() << "Ray-tracing pipelines are unavailable.";
		}
		const auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		EXPECT_EQ(Commands.mValue->DispatchRays(1u << 31, 1u << 31, 4).mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(Commands.mValue->DispatchRays(UINT32_MAX, UINT32_MAX, UINT32_MAX).mCode,
		    EArdaRHIResult::InvalidArgument);
		ASSERT_TRUE(Commands.mValue->Close());
	}

	TEST_P(FArdaSubmissionTest, RecordingFailureCannotBeSubmittedAndResetClearsFailure)
	{
		const auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		Commands.mValue->SetPushConstants(nullptr, sizeof(uint32_t));
		EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(mDevice->ExecuteCommandList(Commands.mValue).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(mDevice->ExecuteCommandLists({Commands.mValue}, EArdaRHIQueueType::Graphics).mStatus.mCode,
		    EArdaRHIResult::InvalidArgument);
		ASSERT_TRUE(Commands.mValue->Reset());
		ASSERT_TRUE(Commands.mValue->Close());
		const auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
		ASSERT_TRUE(Submitted);
		ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
	}

	const char* const NativeBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
	    "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
	    "native-vulkan",
#endif
	};
	INSTANTIATE_TEST_SUITE_P(NativeProviders, FArdaSubmissionTest, testing::ValuesIn(NativeBackends));
}
