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
    EXPECT_FALSE(GetQueueCapabilities().mbGraphics);
    EXPECT_FALSE(GetQueueCapabilities().mbCompute);
    EXPECT_FALSE(GetQueueCapabilities().mbCopy);
    EXPECT_STREQ(ToString(EArdaBackendType::D3D12), "D3D12");
    EXPECT_STREQ(ToString(EArdaBackendType::Vulkan), "Vulkan");
    EXPECT_STREQ(GetModuleName(), "ArdaBackend");
}

TEST(ArdaBackend, ReportsQueueAvailabilityByNvrhiQueueType)
{
    arda::backend::FArdaQueueCapabilities Capabilities;
    Capabilities.mbGraphics = true;
    Capabilities.mbCopy = true;

    EXPECT_TRUE(Capabilities.IsQueueAvailable(nvrhi::CommandQueue::Graphics));
    EXPECT_FALSE(Capabilities.IsQueueAvailable(nvrhi::CommandQueue::Compute));
    EXPECT_TRUE(Capabilities.IsQueueAvailable(nvrhi::CommandQueue::Copy));
    EXPECT_FALSE(Capabilities.IsQueueAvailable(nvrhi::CommandQueue::Count));
}

namespace
{
    void VerifyDeviceInitialization(
        arda::backend::EArdaBackendType Backend,
        bool bRequireComputeAndCopy)
    {
        using namespace arda::backend;

        ShutdownBackend();
        ASSERT_TRUE(ConfigureBackend(Backend));
        if (!InitializeBackend())
        {
            GTEST_SKIP() << GetBackendError();
        }

        EXPECT_TRUE(IsBackendInitialized());
        EXPECT_NE(GetDevice(), nullptr);
        EXPECT_NE(GetDeviceContext().mDevice, nullptr);
        EXPECT_EQ(GetDeviceContext().mBackend, Backend);
        EXPECT_EQ(gCurrentBackend, Backend);

        nvrhi::IDevice* const Device = GetDevice().Get();
        const auto& Capabilities = GetQueueCapabilities();
        EXPECT_EQ(
            &Capabilities,
            &GetDeviceContext().mQueueCapabilities);
        EXPECT_TRUE(Capabilities.mbGraphics);
        EXPECT_EQ(
            Capabilities.mbCompute,
            Device->queryFeatureSupport(nvrhi::Feature::ComputeQueue));
        EXPECT_EQ(
            Capabilities.mbCopy,
            Device->queryFeatureSupport(nvrhi::Feature::CopyQueue));
        if (bRequireComputeAndCopy)
        {
            EXPECT_TRUE(Capabilities.mbCompute);
            EXPECT_TRUE(Capabilities.mbCopy);
        }

        constexpr nvrhi::CommandQueue Queues[] = {
            nvrhi::CommandQueue::Graphics,
            nvrhi::CommandQueue::Compute,
            nvrhi::CommandQueue::Copy
        };
        for (const nvrhi::CommandQueue Queue : Queues)
        {
            if (!Capabilities.IsQueueAvailable(Queue))
            {
                continue;
            }

            const auto CommandList = Device->createCommandList(
                nvrhi::CommandListParameters().setQueueType(Queue));
            ASSERT_NE(CommandList, nullptr);
            CommandList->open();
            CommandList->close();
            EXPECT_NE(Device->executeCommandList(CommandList, Queue), 0);
        }
        EXPECT_TRUE(Device->waitForIdle());

        ShutdownBackend();
        EXPECT_FALSE(IsBackendInitialized());
        EXPECT_FALSE(GetQueueCapabilities().mbGraphics);
        EXPECT_FALSE(GetQueueCapabilities().mbCompute);
        EXPECT_FALSE(GetQueueCapabilities().mbCopy);
    }
}

#if defined(_WIN32)
TEST(ArdaBackend, InitializesD3D12Device)
{
    VerifyDeviceInitialization(arda::backend::EArdaBackendType::D3D12, true);
}
#endif

TEST(ArdaBackend, InitializesVulkanDevice)
{
    VerifyDeviceInitialization(arda::backend::EArdaBackendType::Vulkan, false);
}
