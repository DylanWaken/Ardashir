#include "ArdaDevice.h"
#include "RHIWrappers/ArdaRHICapabilities.h"
#include "RHIWrappers/ArdaRHIRef.h"
#include "RHIWrappers/ArdaRHIResource.h"
#include "RHIWrappers/ArdaRHIResources.h"

#include <gtest/gtest.h>

#include <atomic>

namespace
{
    class FFakeResource final : public arda::rhi::IArdaRHIResource
    {
    public:
        explicit FFakeResource(std::atomic<int>& Destructions)
            : mDestructions(Destructions) {}

        void AddRef() noexcept override { ++mReferences; }
        void Release() noexcept override
        {
            if (--mReferences == 0)
                delete this;
        }
        arda::rhi::EArdaRHIResourceType GetResourceType() const noexcept override
        {
            return arda::rhi::EArdaRHIResourceType::Buffer;
        }
        const char* GetDebugName() const noexcept override { return "Fake"; }

    private:
        ~FFakeResource() override { ++mDestructions; }
        std::atomic<uint32_t> mReferences{ 0 };
        std::atomic<int>& mDestructions;
    };
}

TEST(ArdaRHI, IntrusiveReferencesCopyMoveAndRelease)
{
    using namespace arda::rhi;
    std::atomic<int> Destructions{ 0 };

    TArdaRHIRef<IArdaRHIResource> A(new FFakeResource(Destructions));
    EXPECT_TRUE(A);
    {
        auto B = A;
        auto C = std::move(B);
        EXPECT_FALSE(B);
        EXPECT_EQ(C.Get(), A.Get());
        C.Reset();
        EXPECT_EQ(Destructions.load(), 0);
    }
    A.Reset();
    EXPECT_EQ(Destructions.load(), 1);
}

TEST(ArdaRHI, DescriptorEqualityAndHashAreStable)
{
    using namespace arda::rhi;
    FArdaRHITextureDesc A;
    A.mWidth = 128;
    A.mHeight = 64;
    A.mFormat = EArdaRHIFormat::RGBA8UNorm;
    A.mDebugName = "Color";
    const FArdaRHITextureDesc B = A;

    EXPECT_EQ(A, B);
    EXPECT_EQ(HashValue(A), HashValue(B));

    FArdaRHITextureDesc C = A;
    C.mMipLevels = 2;
    EXPECT_FALSE(A == C);
    EXPECT_NE(HashValue(A), HashValue(C));
}

TEST(ArdaRHI, CacheKeyDescriptorsIgnoreDebugLabels)
{
    using namespace arda::rhi;
    FArdaRHISamplerDesc A;
    A.mDebugName = "First";
    FArdaRHISamplerDesc B = A;
    B.mDebugName = "Second";
    EXPECT_EQ(A, B);
    EXPECT_EQ(HashValue(A), HashValue(B));

    FArdaRHIBindingLayoutDesc LayoutA;
    LayoutA.mVisibility = EArdaRHIShaderStage::Pixel;
    LayoutA.mItems.push_back({ 0, 1, EArdaRHIBindingType::Sampler });
    FArdaRHIBindingLayoutDesc LayoutB = LayoutA;
    LayoutB.mDebugName = "Diagnostic-only";
    EXPECT_EQ(LayoutA, LayoutB);
    EXPECT_EQ(HashValue(LayoutA), HashValue(LayoutB));
    EXPECT_TRUE(Validate(LayoutA));
}

TEST(ArdaRHI, SamplerCacheReusesEvictsAndTrims)
{
    using namespace arda;
    backend::ShutdownBackend();
    backend::FArdaBackendConfiguration Configuration;
    Configuration.mbEnableValidation = false;
    ASSERT_TRUE(backend::ConfigureBackend(Configuration));
    if (!backend::InitializeBackend())
        GTEST_SKIP() << backend::GetBackendError().c_str();

    rhi::FArdaRHIDeviceRef Device = backend::GetDevice();
    ASSERT_TRUE(Device);
    rhi::FArdaRHISamplerDesc Desc;
    auto First = Device->CreateSampler(Desc);
    auto Reused = Device->CreateSampler(Desc);
    ASSERT_TRUE(First);
    ASSERT_TRUE(Reused);
    EXPECT_EQ(First.mValue.Get(), Reused.mValue.Get());

    rhi::FArdaRHIBindingLayoutDesc LayoutDesc;
    LayoutDesc.mVisibility = rhi::EArdaRHIShaderStage::Pixel;
    LayoutDesc.mItems.push_back(
        { 0, 1, rhi::EArdaRHIBindingType::Sampler });
    auto LayoutA = Device->CreateBindingLayout(LayoutDesc);
    auto LayoutB = Device->CreateBindingLayout(LayoutDesc);
    ASSERT_TRUE(LayoutA);
    ASSERT_TRUE(LayoutB);
    EXPECT_EQ(LayoutA.mValue.Get(), LayoutB.mValue.Get());

    rhi::FArdaRHIRasterState RasterDesc;
    auto RasterA = Device->CreateRasterState(RasterDesc);
    auto RasterB = Device->CreateRasterState(RasterDesc);
    ASSERT_TRUE(RasterA);
    ASSERT_TRUE(RasterB);
    EXPECT_EQ(RasterA.mValue.Get(), RasterB.mValue.Get());

    auto TextureReference = Device->CreateTextureReference();
    ASSERT_TRUE(TextureReference);
    EXPECT_FALSE(TextureReference.mValue->GetTexture());

    for (uint32_t Index = 1; Index <= 64; ++Index)
    {
        rhi::FArdaRHISamplerDesc Unique = Desc;
        Unique.mMipBias = static_cast<float>(Index);
        ASSERT_TRUE(Device->CreateSampler(Unique));
    }
    auto Recreated = Device->CreateSampler(Desc);
    ASSERT_TRUE(Recreated);
    EXPECT_NE(First.mValue.Get(), Recreated.mValue.Get());
    EXPECT_TRUE(First.mValue);
    EXPECT_LE(Device->GetDescriptorCacheStats().mSamplers, 64u);

    Device->TrimDescriptorCaches();
    EXPECT_EQ(Device->GetDescriptorCacheStats().mSamplers, 0u);
    EXPECT_EQ(Device->GetDescriptorCacheStats().mBindingLayouts, 0u);
    EXPECT_EQ(Device->GetDescriptorCacheStats().mRasterStates, 0u);
    EXPECT_TRUE(First.mValue);
    First.mValue = nullptr;
    Reused.mValue = nullptr;
    Recreated.mValue = nullptr;
    LayoutA.mValue = nullptr;
    LayoutB.mValue = nullptr;
    RasterA.mValue = nullptr;
    RasterB.mValue = nullptr;
    TextureReference.mValue = nullptr;
    Device = nullptr;
    backend::ShutdownBackend();
}

TEST(ArdaRHI, NativeImportRejectsNonPortableTransferredOwnership)
{
    using namespace arda;
    backend::ShutdownBackend();
    backend::FArdaBackendConfiguration Configuration;
    Configuration.mbEnableValidation = false;
    ASSERT_TRUE(backend::ConfigureBackend(Configuration));
    if (!backend::InitializeBackend())
        GTEST_SKIP() << backend::GetBackendError().c_str();

    rhi::FArdaRHINativeTextureImportDesc Desc;
    Desc.mNativeObject = 1;
    Desc.mOwnership = rhi::EArdaRHINativeOwnership::Transferred;
    Desc.mTexture.mFormat = rhi::EArdaRHIFormat::RGBA8UNorm;
    const auto Result = backend::GetDevice()->ImportNativeTexture(Desc);
    EXPECT_FALSE(Result);
    EXPECT_EQ(Result.mStatus.mCode, rhi::EArdaRHIResult::Unsupported);
    backend::ShutdownBackend();
}

TEST(ArdaRHI, BindingItemsRetainTheirResources)
{
    using namespace arda::rhi;
    std::atomic<int> Destructions{ 0 };
    TArdaRHIRef<IArdaRHIResource> Resource(new FFakeResource(Destructions));

    {
        FArdaRHIBindingItem Item;
        Item.mResource = Resource;
        Resource.Reset();
        EXPECT_EQ(Destructions.load(), 0);
    }

    EXPECT_EQ(Destructions.load(), 1);
}

TEST(ArdaRHI, QueueCapabilitiesUseArdaQueueTypes)
{
    arda::rhi::FArdaRHICapabilities Capabilities;
    Capabilities.mbComputeQueue = true;

    EXPECT_TRUE(Capabilities.IsQueueSupported(arda::rhi::EArdaRHIQueueType::Graphics));
    EXPECT_TRUE(Capabilities.IsQueueSupported(arda::rhi::EArdaRHIQueueType::Compute));
    EXPECT_FALSE(Capabilities.IsQueueSupported(arda::rhi::EArdaRHIQueueType::Copy));
}

TEST(ArdaRHI, AdvancedResourceDescriptorsRemainBackendOpaque)
{
    using namespace arda::rhi;

    FArdaRHIAccelStructDesc AccelStruct;
    AccelStruct.mbTopLevel = true;
    AccelStruct.mTopLevelMaxInstances = 16;
    AccelStruct.mBuildFlags =
        EArdaRHIAccelStructBuildFlags::AllowUpdate |
        EArdaRHIAccelStructBuildFlags::PreferFastTrace;

    FArdaRHIBindlessLayoutDesc Bindless;
    Bindless.mVisibility =
        EArdaRHIShaderStage::Compute | EArdaRHIShaderStage::Pixel;
    Bindless.mMaxCapacity = 1024;
    Bindless.mRegisterSpaces.push_back(
        {0, 1, EArdaRHIBindingType::TextureSRV});

    EXPECT_TRUE(HasAnyFlags(
        AccelStruct.mBuildFlags,
        EArdaRHIAccelStructBuildFlags::AllowUpdate));
    EXPECT_TRUE(HasAnyFlags(
        Bindless.mVisibility, EArdaRHIShaderStage::Compute));
    EXPECT_EQ(Bindless.mRegisterSpaces.size(), 1u);
    EXPECT_EQ(
        static_cast<uint16_t>(EArdaRHIShaderStage::RayGeneration),
        0x100u);
}
