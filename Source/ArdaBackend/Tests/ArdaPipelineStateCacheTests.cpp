#include "ArdaDevice.h"
#include "PipelineStateCache/ArdaPipelineStateCache.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>

namespace
{
    class FTestTexture final : public arda::rhi::IArdaRHITexture
    {
    public:
        explicit FTestTexture(arda::rhi::FArdaRHITextureDesc Desc)
            : mDesc(eastl::move(Desc)) {}
        void AddRef() noexcept override { ++mReferences; }
        void Release() noexcept override
        {
            if (--mReferences == 0)
                delete this;
        }
        arda::rhi::EArdaRHIResourceType GetResourceType() const noexcept override
        {
            return arda::rhi::EArdaRHIResourceType::Texture;
        }
        const char* GetDebugName() const noexcept override { return "TestTexture"; }
        const arda::rhi::FArdaRHITextureDesc& GetDesc() const noexcept override
        {
            return mDesc;
        }
        const void* GetPhysicalIdentity() const noexcept override { return this; }

    private:
        uint32_t mReferences = 0;
        arda::rhi::FArdaRHITextureDesc mDesc;
    };

    class FTestFramebuffer final : public arda::rhi::IArdaRHIFramebuffer
    {
    public:
        explicit FTestFramebuffer(arda::rhi::FArdaRHIFramebufferDesc Desc)
            : mDesc(eastl::move(Desc)) {}
        void AddRef() noexcept override { ++mReferences; }
        void Release() noexcept override
        {
            if (--mReferences == 0)
                delete this;
        }
        arda::rhi::EArdaRHIResourceType GetResourceType() const noexcept override
        {
            return arda::rhi::EArdaRHIResourceType::Framebuffer;
        }
        const char* GetDebugName() const noexcept override { return "TestFramebuffer"; }
        const arda::rhi::FArdaRHIFramebufferDesc& GetDesc() const noexcept override
        {
            return mDesc;
        }

    private:
        uint32_t mReferences = 0;
        arda::rhi::FArdaRHIFramebufferDesc mDesc;
    };

    arda::rhi::TArdaRHIResult<arda::rhi::FArdaRHIShaderRef> CreateTestShader(
        arda::rhi::IArdaRHIDevice& Device,
        arda::backend::EArdaBackendType Backend,
        const char* Artifact,
        const char* EntryPoint,
        arda::rhi::EArdaRHIShaderStage Stage)
    {
        using namespace arda;
        const std::filesystem::path Path =
            std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) /
            (eastl::string(Artifact) +
             backend::GetShaderArtifactExtension(Backend)).c_str();
        auto Bytecode = backend::LoadShaderBytecode(Path);
        if (!Bytecode)
        {
            return {
                {},
                rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidState,
                    Bytecode.mDiagnostic.mMessage.c_str())
            };
        }
        rhi::FArdaRHIShaderDesc Desc;
        Desc.mStage = Stage;
        Desc.mBytecode = Bytecode.mBytecode.data();
        Desc.mBytecodeSize = Bytecode.mBytecode.size();
        Desc.mEntryPoint = EntryPoint;
        Desc.mDebugName = Artifact;
        return Device.CreateShader(Desc);
    }
}

TEST(ArdaPipelineStateCache, CachesPrecachesEvictsAndReportsFailures)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    ShutdownBackend();
    FArdaBackendConfiguration BackendConfiguration;
    BackendConfiguration.mbEnableValidation = false;
    ASSERT_TRUE(ConfigureBackend(BackendConfiguration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    {
        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        auto ShaderA = CreateTestShader(
            *Device, GetDeviceContext().mBackend, "ArdaShaderStructTest",
            "ShaderStructTestCS", EArdaRHIShaderStage::Compute);
        auto ShaderB = CreateTestShader(
            *Device, GetDeviceContext().mBackend, "ArdaShaderStructTest",
            "ShaderStructTestCS", EArdaRHIShaderStage::Compute);
        auto ShaderC = CreateTestShader(
            *Device, GetDeviceContext().mBackend, "ArdaShaderStructTest",
            "ShaderStructTestCS", EArdaRHIShaderStage::Compute);
        ASSERT_TRUE(ShaderA);
        ASSERT_TRUE(ShaderB);
        ASSERT_TRUE(ShaderC);
        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
        LayoutDesc.mItems.push_back(
            { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(Layout);

        FArdaPipelineStateCacheConfiguration Configuration;
        Configuration.mMaxComputeEntries = 2;
        Configuration.mMaxGraphicsEntries = 2;
        FArdaPipelineStateCache Cache(Device, Configuration);
        FArdaComputePipelineStateInitializer A;
        A.mDesc.mComputeShader = ShaderA.mValue;
        A.mDesc.mBindingLayouts.push_back(Layout.mValue);
        A.mDesc.mDebugName = "Compute A";
        auto DirectFirst = Device->CreateComputePipeline(A.mDesc);
        auto DirectSecond = Device->CreateComputePipeline(A.mDesc);
        ASSERT_TRUE(DirectFirst);
        ASSERT_TRUE(DirectSecond);
        EXPECT_NE(DirectFirst.mValue.Get(), DirectSecond.mValue.Get());
        EXPECT_EQ(Device->GetDescriptorCacheStats().mComputePipelines, 0u);
        ASSERT_TRUE(Cache.PrecacheCompute(A));
        EXPECT_EQ(Cache.GetStats().mMisses, 1u);

        FArdaRHIComputePipelineRef First;
        ASSERT_TRUE(Cache.GetOrCreateCompute(A, First));
        ASSERT_TRUE(First);
        EXPECT_EQ(Cache.GetStats().mHits, 1u);
        EXPECT_EQ(Cache.GetStats().mWaits, 0u);

        auto Relabeled = A;
        Relabeled.mDesc.mDebugName = "Diagnostic label only";
        FArdaRHIComputePipelineRef RelabeledPipeline;
        ASSERT_TRUE(Cache.GetOrCreateCompute(Relabeled, RelabeledPipeline));
        EXPECT_EQ(RelabeledPipeline.Get(), First.Get());

        auto CommandList = Device->CreateCommandList(EArdaRHIQueueType::Compute);
        ASSERT_TRUE(CommandList);
        ASSERT_TRUE(CommandList.mValue->Open());
        EXPECT_TRUE(Cache.SetComputePipelineState(
            *CommandList.mValue, Relabeled, {}));
        EXPECT_TRUE(CommandList.mValue->Close());

        FArdaComputePipelineStateInitializer B;
        B.mDesc.mComputeShader = ShaderB.mValue;
        B.mDesc.mBindingLayouts.push_back(Layout.mValue);
        FArdaComputePipelineStateInitializer C;
        C.mDesc.mComputeShader = ShaderC.mValue;
        C.mDesc.mBindingLayouts.push_back(Layout.mValue);
        ASSERT_TRUE(Cache.PrecacheCompute(B));
        ASSERT_TRUE(Cache.PrecacheCompute(C));
        EXPECT_EQ(Cache.GetStats().mComputeEntries, 2u);
        const uint64_t MissesBeforeRecreate = Cache.GetStats().mMisses;
        ASSERT_TRUE(Cache.GetOrCreateCompute(A, First));
        EXPECT_EQ(Cache.GetStats().mMisses, MissesBeforeRecreate + 1);

        Cache.Trim(1, 1);
        EXPECT_EQ(Cache.GetStats().mComputeEntries, 1u);
        Cache.Clear();
        EXPECT_EQ(Cache.GetStats().mComputeEntries, 0u);
        FArdaRHIComputePipelineRef AfterClear;
        ASSERT_TRUE(Cache.GetOrCreateCompute(A, AfterClear));
        EXPECT_NE(AfterClear.Get(), First.Get());

        const auto WrongDevice = Cache.PrecacheCompute(
            A, reinterpret_cast<const IArdaRHIDevice*>(uintptr_t{1}));
        EXPECT_EQ(WrongDevice.mCode, EArdaRHIResult::WrongDevice);

        FArdaComputePipelineStateInitializer Invalid;
        Invalid.mDesc.mDebugName = "Invalid compute";
        EXPECT_FALSE(Cache.PrecacheCompute(Invalid));
        EXPECT_EQ(Cache.GetStats().mCreateFailures, 1u);
        ASSERT_FALSE(Cache.GetDiagnostics().empty());
        EXPECT_EQ(
            Cache.GetDiagnostics().back().mCode,
            EArdaRHIResult::InvalidArgument);
    }

    ShutdownBackend();
}

TEST(ArdaPipelineStateCache, ResolvesFramebufferFormatsAndRejectsMismatches)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    ShutdownBackend();
    FArdaBackendConfiguration BackendConfiguration;
    BackendConfiguration.mbEnableValidation = false;
    ASSERT_TRUE(ConfigureBackend(BackendConfiguration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    {
        FArdaRHIDeviceRef Device = GetDevice();
        auto VertexShader = CreateTestShader(
            *Device, GetDeviceContext().mBackend, "ArdaPipelineStateTestVS",
            "PipelineStateTestVS", EArdaRHIShaderStage::Vertex);
        auto PixelShader = CreateTestShader(
            *Device, GetDeviceContext().mBackend, "ArdaPipelineStateTestPS",
            "PipelineStateTestPS", EArdaRHIShaderStage::Pixel);
        ASSERT_TRUE(VertexShader);
        ASSERT_TRUE(PixelShader);

        FArdaRHITextureDesc ColorDesc;
        ColorDesc.mWidth = 16;
        ColorDesc.mHeight = 16;
        ColorDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
        ColorDesc.mUsage = EArdaRHITextureUsage::RenderTarget;
        FArdaRHITextureRef Color(new FTestTexture(ColorDesc));
        FArdaRHITextureDesc DepthDesc = ColorDesc;
        DepthDesc.mFormat = EArdaRHIFormat::D32;
        DepthDesc.mUsage = EArdaRHITextureUsage::DepthStencil;
        FArdaRHITextureRef Depth(new FTestTexture(DepthDesc));

        FArdaRHIFramebufferDesc FramebufferDesc;
        FramebufferDesc.mColorAttachments.push_back({ Color, {} });
        FramebufferDesc.mDepthAttachment = { Depth, {} };
        FArdaRHIFramebufferRef Framebuffer(
            new FTestFramebuffer(FramebufferDesc));

        FArdaGraphicsPipelineStateInitializer Initializer;
        Initializer.mDesc.mVertexShader = VertexShader.mValue;
        Initializer.mDesc.mPixelShader = PixelShader.mValue;
        Initializer.mDesc.mDebugName = "Derived graphics";
        FArdaPipelineStateCache Cache(Device);
        FArdaRHIGraphicsPipelineRef Pipeline;
        ASSERT_TRUE(Cache.GetOrCreateGraphics(
            Initializer, Framebuffer, Pipeline));
        ASSERT_TRUE(Pipeline);
        ASSERT_EQ(Pipeline->GetDesc().mColorFormats.size(), 1u);
        EXPECT_EQ(
            Pipeline->GetDesc().mColorFormats[0],
            EArdaRHIFormat::RGBA8UNorm);
        EXPECT_EQ(Pipeline->GetDesc().mDepthFormat, EArdaRHIFormat::D32);
        EXPECT_EQ(Pipeline->GetDesc().mSampleCount, 1u);

        auto Relabeled = Initializer;
        Relabeled.mDesc.mDebugName = "Another graphics label";
        FArdaRHIGraphicsPipelineRef Reused;
        ASSERT_TRUE(Cache.GetOrCreateGraphics(
            Relabeled, Framebuffer, Reused));
        EXPECT_EQ(Reused.Get(), Pipeline.Get());
        auto DirectFirst = Device->CreateGraphicsPipeline(Pipeline->GetDesc());
        auto DirectSecond = Device->CreateGraphicsPipeline(Pipeline->GetDesc());
        ASSERT_TRUE(DirectFirst);
        ASSERT_TRUE(DirectSecond);
        EXPECT_NE(DirectFirst.mValue.Get(), DirectSecond.mValue.Get());
        EXPECT_EQ(Device->GetDescriptorCacheStats().mGraphicsPipelines, 0u);

        FArdaRHITextureDesc NativeDepthDesc;
        NativeDepthDesc.mWidth = 16;
        NativeDepthDesc.mHeight = 16;
        NativeDepthDesc.mFormat = EArdaRHIFormat::D32;
        NativeDepthDesc.mUsage = EArdaRHITextureUsage::DepthStencil;
        NativeDepthDesc.mInitialState = EArdaRHIResourceState::DepthWrite;
        NativeDepthDesc.mbKeepInitialState = true;
        auto NativeDepth = Device->CreateTexture(NativeDepthDesc);
        ASSERT_TRUE(NativeDepth) << NativeDepth.mStatus.mMessage.c_str();
        FArdaRHIFramebufferDesc NativeFramebufferDesc;
        NativeFramebufferDesc.mDepthAttachment = { NativeDepth.mValue, {} };
        auto NativeFramebuffer =
            Device->CreateFramebuffer(NativeFramebufferDesc);
        ASSERT_TRUE(NativeFramebuffer)
            << NativeFramebuffer.mStatus.mMessage.c_str();

        FArdaGraphicsPipelineStateInitializer NativeInitializer;
        NativeInitializer.mDesc.mVertexShader = VertexShader.mValue;
        NativeInitializer.mDesc.mDebugName = "Native graphics";
        const uint64_t MissesBeforePrecache = Cache.GetStats().mMisses;
        ASSERT_TRUE(Cache.PrecacheGraphics(
            NativeInitializer, NativeFramebuffer.mValue));
        EXPECT_EQ(Cache.GetStats().mMisses, MissesBeforePrecache + 1);
        const uint64_t HitsBeforeBind = Cache.GetStats().mHits;
        auto CommandList =
            Device->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(CommandList);
        ASSERT_TRUE(CommandList.mValue->Open());
        FArdaRHIGraphicsState DynamicState;
        DynamicState.mFramebuffer = NativeFramebuffer.mValue;
        DynamicState.mViewports.push_back({ 1.f, 15.f, 2.f, 14.f, 0.f, 1.f });
        DynamicState.mScissors.push_back({ 1, 15, 2, 14 });
        ASSERT_TRUE(Cache.SetGraphicsPipelineState(
            *CommandList.mValue, NativeInitializer,
            eastl::move(DynamicState)));
        EXPECT_EQ(Cache.GetStats().mHits, HitsBeforeBind + 1);
        EXPECT_TRUE(CommandList.mValue->Close());

        FArdaRHIFramebufferDesc EmptyFramebufferDesc;
        auto EmptyFramebuffer =
            Device->CreateFramebuffer(EmptyFramebufferDesc);
        EXPECT_FALSE(EmptyFramebuffer);
        auto BadBindCommandList =
            Device->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(BadBindCommandList);
        ASSERT_TRUE(BadBindCommandList.mValue->Open());
        FArdaRHIGraphicsState BadBindState;
        BadBindState.mFramebuffer = Framebuffer;
        EXPECT_EQ(
            Cache.SetGraphicsPipelineState(
                *BadBindCommandList.mValue, Initializer,
                eastl::move(BadBindState)).mCode,
            EArdaRHIResult::WrongDevice);
        EXPECT_TRUE(BadBindCommandList.mValue->Close());

        Cache.Clear();
        FArdaRHIGraphicsPipelineRef AfterClear;
        ASSERT_TRUE(Cache.GetOrCreateGraphics(
            Initializer, Framebuffer, AfterClear));
        EXPECT_NE(AfterClear.Get(), Pipeline.Get());

        auto MismatchedFormat = Initializer;
        MismatchedFormat.mDesc.mColorFormats.push_back(
            EArdaRHIFormat::BGRA8UNorm);
        EXPECT_EQ(
            Cache.GetOrCreateGraphics(
                MismatchedFormat, Framebuffer, Reused).mCode,
            EArdaRHIResult::InvalidArgument);

        auto MismatchedSamples = Initializer;
        MismatchedSamples.mDesc.mSampleCount = 2;
        EXPECT_EQ(
            Cache.GetOrCreateGraphics(
                MismatchedSamples, Framebuffer, Reused).mCode,
            EArdaRHIResult::InvalidArgument);
    }

    ShutdownBackend();
}

TEST(ArdaPipelineStateCache, DeterministicLruKeepsMostRecentlyUsedEntry)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    ShutdownBackend();
    FArdaBackendConfiguration BackendConfiguration;
    BackendConfiguration.mbEnableValidation = false;
    ASSERT_TRUE(ConfigureBackend(BackendConfiguration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    {
        FArdaRHIDeviceRef Device = GetDevice();
        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
        LayoutDesc.mItems.push_back(
            { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(Layout);

        FArdaComputePipelineStateInitializer Initializers[3];
        for (auto& Initializer : Initializers)
        {
            auto Shader = CreateTestShader(
                *Device, GetDeviceContext().mBackend,
                "ArdaShaderStructTest", "ShaderStructTestCS",
                EArdaRHIShaderStage::Compute);
            ASSERT_TRUE(Shader);
            Initializer.mDesc.mComputeShader = Shader.mValue;
            Initializer.mDesc.mBindingLayouts.push_back(Layout.mValue);
        }

        FArdaPipelineStateCacheConfiguration Configuration;
        Configuration.mMaxComputeEntries = 2;
        FArdaPipelineStateCache Cache(Device, Configuration);
        FArdaRHIComputePipelineRef A;
        FArdaRHIComputePipelineRef B;
        FArdaRHIComputePipelineRef C;
        ASSERT_TRUE(Cache.GetOrCreateCompute(Initializers[0], A));
        ASSERT_TRUE(Cache.GetOrCreateCompute(Initializers[1], B));
        FArdaRHIComputePipelineRef AHit;
        ASSERT_TRUE(Cache.GetOrCreateCompute(Initializers[0], AHit));
        EXPECT_EQ(AHit.Get(), A.Get());
        ASSERT_TRUE(Cache.GetOrCreateCompute(Initializers[2], C));

        FArdaRHIComputePipelineRef AStillCached;
        ASSERT_TRUE(Cache.GetOrCreateCompute(
            Initializers[0], AStillCached));
        EXPECT_EQ(AStillCached.Get(), A.Get());
        FArdaRHIComputePipelineRef BRecreated;
        ASSERT_TRUE(Cache.GetOrCreateCompute(
            Initializers[1], BRecreated));
        EXPECT_NE(BRecreated.Get(), B.Get());
        EXPECT_EQ(Cache.GetStats().mComputeEntries, 2u);
    }

    ShutdownBackend();
}

TEST(ArdaPipelineStateCache, ConcurrentSameKeyCreatesOneOuterEntry)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    ShutdownBackend();
    FArdaBackendConfiguration BackendConfiguration;
    BackendConfiguration.mbEnableValidation = false;
    ASSERT_TRUE(ConfigureBackend(BackendConfiguration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    {
        FArdaRHIDeviceRef Device = GetDevice();
        auto Shader = CreateTestShader(
            *Device, GetDeviceContext().mBackend,
            "ArdaShaderStructTest", "ShaderStructTestCS",
            EArdaRHIShaderStage::Compute);
        ASSERT_TRUE(Shader);
        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
        LayoutDesc.mItems.push_back(
            { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(Layout);
        FArdaComputePipelineStateInitializer Initializer;
        Initializer.mDesc.mComputeShader = Shader.mValue;
        Initializer.mDesc.mBindingLayouts.push_back(Layout.mValue);

        FArdaPipelineStateCache Cache(Device);
        constexpr size_t ThreadCount = 8;
        std::atomic<size_t> Ready{ 0 };
        std::atomic<bool> Go{ false };
        std::vector<FArdaRHIComputePipelineRef> Pipelines(ThreadCount);
        std::vector<FArdaRHIStatus> Statuses(ThreadCount);
        std::vector<std::thread> Threads;
        Threads.reserve(ThreadCount);
        for (size_t Index = 0; Index < ThreadCount; ++Index)
        {
            Threads.emplace_back(
                [&, Index]
                {
                    Ready.fetch_add(1, std::memory_order_release);
                    while (!Go.load(std::memory_order_acquire))
                        std::this_thread::yield();
                    Statuses[Index] = Cache.GetOrCreateCompute(
                        Initializer, Pipelines[Index]);
                });
        }
        while (Ready.load(std::memory_order_acquire) != ThreadCount)
            std::this_thread::yield();
        Go.store(true, std::memory_order_release);
        for (auto& Thread : Threads)
            Thread.join();

        for (size_t Index = 0; Index < ThreadCount; ++Index)
        {
            EXPECT_TRUE(Statuses[Index]);
            ASSERT_TRUE(Pipelines[Index]);
            EXPECT_EQ(Pipelines[Index].Get(), Pipelines[0].Get());
        }
        const auto Stats = Cache.GetStats();
        EXPECT_EQ(Stats.mMisses, 1u);
        EXPECT_EQ(Stats.mHits, ThreadCount - 1);
        EXPECT_LE(Stats.mWaits, Stats.mHits);
        EXPECT_EQ(Stats.mInFlight, 0u);
        EXPECT_EQ(Stats.mComputeEntries, 1u);
    }

    ShutdownBackend();
}
