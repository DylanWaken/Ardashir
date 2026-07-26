#include "ArdaScene.h"

#include <gtest/gtest.h>

TEST(ArdaScene, ReportsModuleName)
{
    EXPECT_STREQ(arda::scene::GetModuleName(), "ArdaScene");
}
