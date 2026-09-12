#include "ArdaTestBackend.h"
#include "ArdaBackend.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	class FArdaSubmissionTest : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			FArdaBackendConfiguration Configuration;
			Configuration.mBackendName = GetParam();
			Configuration.mbEnableValidation = true;
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
