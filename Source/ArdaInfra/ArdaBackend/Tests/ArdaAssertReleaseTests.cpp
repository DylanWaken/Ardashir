// Exercise fatal control flow with checks disabled even when the test target enables them.
#ifdef ARDA_ENABLE_CHECKS
#undef ARDA_ENABLE_CHECKS
#endif
#ifndef NDEBUG
#define NDEBUG
#endif

#include "RHI/Config/ArdaAssert.h"

#include <gtest/gtest.h>

static_assert(ARDA_CHECK_ENABLED == 0, "Release assertion tests require checks to be disabled.");

namespace
{
	int ReturnValueOrFail(bool bReturnValue)
	{
		if (bReturnValue)
		{
			return 7;
		}
		ARDA_CHECK_MSG("Unconditional release failure %d", 23);
	}
}

TEST(ArdaAssertRelease, ConditionalChecksDoNotEvaluateTheirArguments)
{
	int EvaluationCount = 0;
	ARDA_CHECK(++EvaluationCount == 1);
	ARDA_CHECKF(++EvaluationCount == 1, "Ignored %d", ++EvaluationCount);
	EXPECT_EQ(EvaluationCount, 0);
}

TEST(ArdaAssertRelease, VerifyEvaluatesConditionsWithoutTerminating)
{
	int EvaluationCount = 0;
	EXPECT_FALSE(ARDA_VERIFY(++EvaluationCount == 0));
	EXPECT_FALSE(ARDA_VERIFYF(++EvaluationCount == 0, "Ignored %d", ++EvaluationCount));
	EXPECT_EQ(EvaluationCount, 2);
}

TEST(ArdaAssertRelease, NonfatalControlPathReturnsValue)
{
	EXPECT_EQ(ReturnValueOrFail(true), 7);
}

#if GTEST_HAS_DEATH_TEST
TEST(ArdaAssertReleaseDeathTest, CheckMessageTerminatesNonvoidFunctionWithChecksDisabled)
{
	EXPECT_DEATH(ReturnValueOrFail(false), "Unconditional release failure 23");
}

TEST(ArdaAssertReleaseDeathTest, CheckMessageTerminatesWithoutFormatArguments)
{
	EXPECT_DEATH(ARDA_CHECK_MSG("Unconditional release failure"), "Unconditional release failure");
}
#endif
