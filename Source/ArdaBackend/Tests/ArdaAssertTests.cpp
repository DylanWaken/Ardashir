#include "ArdaBackend.h"

#include <gtest/gtest.h>

namespace
{
    class FArdaAssertTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            arda::backend::SetEnsureBehavior(
                arda::backend::EArdaEnsureBehavior::Log);
        }
    };
}

TEST_F(FArdaAssertTest, EnsureReturnsFalseWithoutTerminating)
{
    EXPECT_FALSE(ARDA_ENSURE(false));
    EXPECT_TRUE(ARDA_ENSURE(true));
    EXPECT_FALSE(ARDA_ENSURE_MSGF(false, "Ensure message %d", 7));
}

#if GTEST_HAS_DEATH_TEST
TEST(ArdaAssertDeathTest, CheckTerminatesOnFailure)
{
    EXPECT_DEATH(
        ARDA_CHECK(false),
        "Assertion failed: false");
}

TEST(ArdaAssertDeathTest, CheckfIncludesFormattedMessage)
{
    EXPECT_DEATH(
        ARDA_CHECKF(false, "Bad value %d", 13),
        "Bad value 13");
}
#endif

TEST(ArdaAssert, VerifyReturnsTrueForValidConditions)
{
    EXPECT_TRUE(ARDA_VERIFY(true));
    EXPECT_TRUE(ARDA_VERIFYF(true, "Ignored %d", 1));
}

#if GTEST_HAS_DEATH_TEST
TEST(ArdaAssertDeathTest, VerifyTerminatesOnFailureWhenChecksAreEnabled)
{
    EXPECT_DEATH(
        (void)ARDA_VERIFY(false),
        "Assertion failed: false");
}
#endif
