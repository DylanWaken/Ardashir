#include "ArdaDL.h"

#include <gtest/gtest.h>

TEST(ArdaDL, ReportsModuleName)
{
    EXPECT_STREQ(arda::dl::GetModuleName(), "ArdaDL");
}
