#include "ArdaBackend.h"

#include <gtest/gtest.h>

TEST(ArdaBackend, ExposesProcessWideConfigurationAndContext)
{
    using namespace arda::backend;

    ShutdownBackend();
    ASSERT_TRUE(ConfigureBackend(DefaultBackend));

    EXPECT_EQ(gCurrentBackend, DefaultBackend);
    EXPECT_EQ(GetBackendConfiguration().mBackend, DefaultBackend);
    EXPECT_EQ(GetDeviceContext().mBackend, DefaultBackend);
    EXPECT_EQ(&GetDeviceContext(), &GetDeviceContext());
    EXPECT_FALSE(IsBackendInitialized());
    EXPECT_EQ(GetDevice(), nullptr);
    EXPECT_STREQ(ToString(EArdaBackendType::D3D12), "D3D12");
    EXPECT_STREQ(ToString(EArdaBackendType::Vulkan), "Vulkan");
    EXPECT_STREQ(GetModuleName(), "ArdaBackend");
}

namespace
{
    void VerifyDeviceInitialization(arda::backend::EArdaBackendType backend)
    {
        using namespace arda::backend;

        ShutdownBackend();
        ASSERT_TRUE(ConfigureBackend(backend));
        if (!InitializeBackend())
        {
            GTEST_SKIP() << GetBackendError();
        }

        EXPECT_TRUE(IsBackendInitialized());
        EXPECT_NE(GetDevice(), nullptr);
        EXPECT_NE(GetDeviceContext().mDevice, nullptr);
        EXPECT_EQ(GetDeviceContext().mBackend, backend);
        EXPECT_EQ(gCurrentBackend, backend);
        ShutdownBackend();
        EXPECT_FALSE(IsBackendInitialized());
    }
}

#if defined(_WIN32)
TEST(ArdaBackend, InitializesD3D12Device)
{
    VerifyDeviceInitialization(arda::backend::EArdaBackendType::D3D12);
}
#endif

TEST(ArdaBackend, InitializesVulkanDevice)
{
    VerifyDeviceInitialization(arda::backend::EArdaBackendType::Vulkan);
}
