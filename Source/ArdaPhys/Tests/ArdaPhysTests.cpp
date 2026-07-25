#include "ArdaPhys.h"

#include <gtest/gtest.h>

TEST(ArdaPhys, ReportsModuleName)
{
    EXPECT_STREQ(arda::phys::GetModuleName(), "ArdaPhys");
}
