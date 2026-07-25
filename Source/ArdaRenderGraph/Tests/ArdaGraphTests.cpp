#include "ArdaRenderGraph.h"

#include <gtest/gtest.h>

TEST(ArdaRenderGraph, ReportsModuleName)
{
    EXPECT_STREQ(arda::render_graph::GetModuleName(), "ArdaRenderGraph");
}
