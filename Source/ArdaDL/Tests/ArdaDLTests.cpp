#include "ArdaDL.h"

#include <gtest/gtest.h>

TEST(ArdaDL, ReportsModuleName)
{
    EXPECT_STREQ(arda::GetDLModuleName(), "ArdaDL");
}
