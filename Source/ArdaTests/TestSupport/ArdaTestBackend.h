/** @file ArdaTestBackend.h
 * GoogleTest initialization policy shared by backend and RDG GPU fixtures.
 * Skip only native validation unavailability; preserve all other assertion failures.
 */
#pragma once
#include "ArdaTestValidation.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include <gtest/gtest.h>
#include <atomic>
#include <cstdio>

namespace arda
{
	/** Records native validation errors without turning expected facade status failures into test failures.
	 * Keep this callback alive until all devices and backend instances using it have shut down.
	 */
	class FArdaTestDiagnosticCallback final : public IArdaDiagnosticCallback
	{
	public:
		void Message(EArdaDiagnosticSeverity Severity, const char* Text) override
		{
			if (Severity >= EArdaDiagnosticSeverity::Warning)
			{
				std::fprintf(stderr, "Native GPU diagnostic: %s\n", Text ? Text : "");
			}

			// Native validation can arrive from GPU completion threads; assert the count after retirement.
			if (Severity == EArdaDiagnosticSeverity::Error || Severity == EArdaDiagnosticSeverity::Fatal)
			{
				mErrorCount.fetch_add(1, std::memory_order_relaxed);
			}
		}

		[[nodiscard]] uint32_t GetErrorCount() const noexcept
		{
			return mErrorCount.load(std::memory_order_relaxed);
		}

	private:
		std::atomic<uint32_t> mErrorCount{0};
	};
}

// Like ASSERT_TRUE, this returns from the surrounding test/fixture/helper. Only
// missing validation is a skip; shader, device and other initialization bugs fail.
#define ARDA_REQUIRE_BACKEND()                                                                                         \
	GTEST_AMBIGUOUS_ELSE_BLOCKER_                                                                                      \
	if (const bool ArdaInitialized = ::arda::InitializeBackend(); !ArdaInitialized &&                                  \
	    ::arda::ArdaTestValidationEnabled &&                                                                           \
	    ::arda::GetBackendInitializeResult() == ::arda::EArdaInitializeResult::ValidationUnavailable)                  \
		GTEST_SKIP() << ::arda::GetBackendError().c_str();                                                             \
	else                                                                                                               \
		ASSERT_TRUE(ArdaInitialized) << ::arda::GetBackendError().c_str()

namespace arda
{
	/** Shared lifecycle for native backend fixtures that use the default compiler configuration. */
	class FArdaTestBackendFixture : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			if (!FindBackendModule(GetParam()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mMessageCallback = &mDiagnostics;
			ASSERT_TRUE(ConfigureBackend(Configuration)) << GetBackendError().c_str();
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
		}

		void TearDown() override
		{
			const bool bMissingValidation = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice->RunGarbageCollection();
				mDevice.Reset();
			}
			// Keep the diagnostic callback alive through native device shutdown.
			ShutdownBackend();
			if (!bMissingValidation)
			{
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}

		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};
}
