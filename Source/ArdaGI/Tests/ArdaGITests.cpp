#include "ArdaGI.h"

#include <gtest/gtest.h>

TEST(ArdaGI, ReportsModuleName)
{
    EXPECT_STREQ(arda::gi::GetModuleName(), "ArdaGI");
}
