#include "ArdaBackend.h"
#include "PipelineStateCache/ArdaPipelineStateCache.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace
{
    class FCapturingDiagnosticCallback final
        : public arda::backend::IArdaDiagnosticCallback
    {
    public:
        void Message(
            arda::backend::EArdaDiagnosticSeverity,
            const char* Text) override
        {
            mMessages.emplace_back(Text ? Text : "");
        }

        [[nodiscard]] bool Contains(const char* Text) const
        {
            return std::any_of(
                mMessages.begin(), mMessages.end(),
                [Text](const std::string& Message)
                {
                    return Message.find(Text) != std::string::npos;
                });
        }

        void Clear() { mMessages.clear(); }

    private:
        std::vector<std::string> mMessages;
    };

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
        const char* BackendName,
        const char* Artifact,
        const char* EntryPoint,
        arda::rhi::EArdaRHIShaderStage Stage)
    {
        using namespace arda;
        const std::filesystem::path Path =
            std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) /
            (eastl::string(Artifact) +
             backend::GetShaderArtifactExtension(BackendName)).c_str();
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

    arda::rhi::FArdaRHIStatus CreatePersistentTestPipelines(
        const char* BackendName,
        uint64_t& OutComputeKey,
        uint64_t& OutGraphicsKey,
        uint64_t* OutMeshletKey = nullptr)
    {
        using namespace arda;
        using namespace backend;
        using namespace rhi;

        auto Device = GetDevice();
        auto ComputeShader = CreateTestShader(
            *Device, BackendName, "ArdaShaderStructTest",
            "ShaderStructTestCS", EArdaRHIShaderStage::Compute);
        if (!ComputeShader)
            return ComputeShader.mStatus;
        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
        LayoutDesc.mItems.push_back(
            { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        if (!Layout)
            return Layout.mStatus;
        FArdaComputePipelineStateInitializer ComputeInitializer;
        ComputeInitializer.mDesc.mComputeShader = ComputeShader.mValue;
        ComputeInitializer.mDesc.mBindingLayouts.push_back(Layout.mValue);
        FArdaPipelineStateCache Cache(Device);
        FArdaRHIComputePipelineRef ComputePipeline;
        if (auto Status = Cache.GetOrCreateCompute(
                ComputeInitializer, ComputePipeline);
            !Status)
            return Status;
        OutComputeKey = ComputePipeline->GetDesc().mPersistentCacheKey;

        auto VertexShader = CreateTestShader(
            *Device, BackendName, "ArdaPipelineStateTestVS",
            "PipelineStateTestVS", EArdaRHIShaderStage::Vertex);
        if (!VertexShader)
            return VertexShader.mStatus;
        auto PixelShader = CreateTestShader(
            *Device, BackendName, "ArdaPipelineStateTestPS",
            "PipelineStateTestPS", EArdaRHIShaderStage::Pixel);
        if (!PixelShader)
            return PixelShader.mStatus;
        FArdaGraphicsPipelineStateInitializer GraphicsInitializer;
        GraphicsInitializer.mDesc.mVertexShader = VertexShader.mValue;
        GraphicsInitializer.mDesc.mPixelShader = PixelShader.mValue;
        GraphicsInitializer.mDesc.mColorFormats.push_back(
            EArdaRHIFormat::RGBA8UNorm);
        GraphicsInitializer.mDesc.mSampleCount = 1;
        GraphicsInitializer.mDesc.mDepthStencilState.mbDepthTest = false;
        GraphicsInitializer.mDesc.mDepthStencilState.mbDepthWrite = false;
        FArdaRHIGraphicsPipelineRef GraphicsPipeline;
        if (auto Status = Cache.GetOrCreateGraphics(
                GraphicsInitializer, {}, GraphicsPipeline);
            !Status)
            return Status;
        OutGraphicsKey = GraphicsPipeline->GetDesc().mPersistentCacheKey;

        if (OutMeshletKey != nullptr)
        {
            *OutMeshletKey = 0;
            if (Device->GetCapabilities().mMeshShaderTier !=
                EArdaRHIMeshShaderTier::None)
            {
                auto MeshShader = CreateTestShader(
                    *Device, BackendName, "ArdaMeshPipelineTestMS",
                    "MeshPipelineTestMS", EArdaRHIShaderStage::Mesh);
                if (!MeshShader)
                    return MeshShader.mStatus;
                auto MeshPixelShader = CreateTestShader(
                    *Device, BackendName, "ArdaMeshPipelineTestPS",
                    "MeshPipelineTestPS", EArdaRHIShaderStage::Pixel);
                if (!MeshPixelShader)
                    return MeshPixelShader.mStatus;
                FArdaMeshletPipelineStateInitializer MeshletInitializer;
                MeshletInitializer.mDesc.mMeshShader = MeshShader.mValue;
                MeshletInitializer.mDesc.mPixelShader = MeshPixelShader.mValue;
                MeshletInitializer.mDesc.mColorFormats.push_back(
                    EArdaRHIFormat::RGBA8UNorm);
                MeshletInitializer.mDesc.mSampleCount = 1;
                MeshletInitializer.mDesc.mRasterState.mCullMode =
                    EArdaRHICullMode::None;
                MeshletInitializer.mDesc.mDepthStencilState.mbDepthTest = false;
                MeshletInitializer.mDesc.mDepthStencilState.mbDepthWrite = false;
                FArdaRHIMeshletPipelineRef MeshletPipeline;
                if (auto Status = Cache.GetOrCreateMeshlet(
                        MeshletInitializer, {}, MeshletPipeline);
                    !Status)
                    return Status;
                *OutMeshletKey =
                    MeshletPipeline->GetDesc().mPersistentCacheKey;
            }
        }
        return {};
    }
}

TEST(ArdaPipelineStateCache, PersistentKeysAreNonSemanticMetadata)
{
    using namespace arda::rhi;
    FArdaRHIComputePipelineDesc ComputeA;
    FArdaRHIComputePipelineDesc ComputeB;
    ComputeA.mPersistentCacheKey = 1;
    ComputeB.mPersistentCacheKey = 2;
    EXPECT_EQ(ComputeA, ComputeB);
    EXPECT_EQ(HashValue(ComputeA), HashValue(ComputeB));

    FArdaRHIGraphicsPipelineDesc GraphicsA;
    FArdaRHIGraphicsPipelineDesc GraphicsB;
    GraphicsA.mPersistentCacheKey = 3;
    GraphicsB.mPersistentCacheKey = 4;
    EXPECT_EQ(GraphicsA, GraphicsB);
    EXPECT_EQ(HashValue(GraphicsA), HashValue(GraphicsB));

    FArdaRHIMeshletPipelineDesc MeshletA;
    FArdaRHIMeshletPipelineDesc MeshletB;
    MeshletA.mPersistentCacheKey = 5;
    MeshletB.mPersistentCacheKey = 6;
    EXPECT_EQ(MeshletA, MeshletB);
    EXPECT_EQ(HashValue(MeshletA), HashValue(MeshletB));
}

#if defined(_WIN32)
TEST(ArdaPipelineStateCache, PersistsReloadsAndRejectsCorruptD3D12Blobs)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    ShutdownBackend();
    const auto Unique = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path Directory =
        std::filesystem::temp_directory_path() /
        ("arda-pso-cache-" + std::to_string(Unique));
    std::error_code Error;
    std::filesystem::remove_all(Directory, Error);

    FArdaBackendConfiguration Configuration;
    FCapturingDiagnosticCallback Diagnostics;
    Configuration.mBackendName = "native-d3d12";
    Configuration.mbEnableValidation = false;
    Configuration.mPipelineCacheDirectory = Directory;
    Configuration.mMessageCallback = &Diagnostics;

    ASSERT_TRUE(ConfigureBackend(Configuration));
    if (!InitializeBackend())
    {
        std::filesystem::remove_all(Directory, Error);
        GTEST_SKIP() << GetBackendError().c_str();
    }
    if (!GetDevice()->GetCapabilities().mbPipelineCachePersistence)
    {
        ShutdownBackend();
        std::filesystem::remove_all(Directory, Error);
        GTEST_SKIP() << "The selected backend does not expose native pipeline-cache persistence.";
    }
    FArdaRHIDeviceRef RetainedFirstDevice = GetDevice();
    uint64_t FirstComputeKey = 0;
    uint64_t FirstGraphicsKey = 0;
    uint64_t FirstMeshletKey = 0;
    const auto FirstCreateStatus =
        CreatePersistentTestPipelines(
            "native-d3d12", FirstComputeKey, FirstGraphicsKey,
            &FirstMeshletKey);
    ASSERT_TRUE(FirstCreateStatus)
        << FirstCreateStatus.mMessage.c_str();
    EXPECT_NE(FirstComputeKey, 0u);
    EXPECT_NE(FirstGraphicsKey, 0u);
    ShutdownBackend();

    const auto CacheFile = Directory / "native-d3d12.pso-cache";
    ASSERT_TRUE(std::filesystem::is_regular_file(CacheFile));
    EXPECT_GT(std::filesystem::file_size(CacheFile), 24u);

    Diagnostics.Clear();
    ASSERT_TRUE(ConfigureBackend(Configuration));
    ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
    uint64_t ReloadedComputeKey = 0;
    uint64_t ReloadedGraphicsKey = 0;
    uint64_t ReloadedMeshletKey = 0;
    const auto ReloadedCreateStatus =
        CreatePersistentTestPipelines(
            "native-d3d12",
            ReloadedComputeKey,
            ReloadedGraphicsKey,
            &ReloadedMeshletKey);
    EXPECT_TRUE(ReloadedCreateStatus)
        << ReloadedCreateStatus.mMessage.c_str();
    EXPECT_EQ(ReloadedComputeKey, FirstComputeKey);
    EXPECT_EQ(ReloadedGraphicsKey, FirstGraphicsKey);
    EXPECT_EQ(ReloadedMeshletKey, FirstMeshletKey);
    EXPECT_TRUE(Diagnostics.Contains("LoadComputePipeline accepted"));
    EXPECT_TRUE(Diagnostics.Contains("LoadGraphicsPipeline accepted"));
    if (FirstMeshletKey != 0)
        EXPECT_TRUE(Diagnostics.Contains("cached D3D12 meshlet PSO"));
    ShutdownBackend();

    {
        const std::string NewerContents = "newer pipeline cache generation";
        std::ofstream Newer(CacheFile, std::ios::binary | std::ios::trunc);
        Newer.write(NewerContents.data(), NewerContents.size());
        ASSERT_TRUE(Newer);
        Newer.close();
        RetainedFirstDevice = nullptr;
        std::ifstream Verify(CacheFile, std::ios::binary);
        const std::string Actual{
            std::istreambuf_iterator<char>(Verify),
            std::istreambuf_iterator<char>() };
        EXPECT_EQ(Actual, NewerContents);
    }

    {
        std::fstream InvalidHeader(
            CacheFile, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(InvalidHeader);
        constexpr uint32_t NonZeroReserved = 1;
        InvalidHeader.seekp(12);
        InvalidHeader.write(
            reinterpret_cast<const char*>(&NonZeroReserved),
            sizeof(NonZeroReserved));
        ASSERT_TRUE(InvalidHeader);
    }
    ASSERT_TRUE(ConfigureBackend(Configuration));
    ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
    uint64_t WrongBackendComputeKey = 0;
    uint64_t WrongBackendGraphicsKey = 0;
    const auto WrongBackendCreateStatus =
        CreatePersistentTestPipelines(
            "native-d3d12",
            WrongBackendComputeKey,
            WrongBackendGraphicsKey);
    EXPECT_TRUE(WrongBackendCreateStatus)
        << WrongBackendCreateStatus.mMessage.c_str();
    ShutdownBackend();

    {
        std::ofstream Corrupt(CacheFile, std::ios::binary | std::ios::trunc);
        Corrupt << "not a pipeline cache";
    }
    ASSERT_TRUE(ConfigureBackend(Configuration));
    ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
    uint64_t CorruptComputeKey = 0;
    uint64_t CorruptGraphicsKey = 0;
    const auto CorruptCreateStatus =
        CreatePersistentTestPipelines(
            "native-d3d12",
            CorruptComputeKey,
            CorruptGraphicsKey);
    EXPECT_TRUE(CorruptCreateStatus)
        << CorruptCreateStatus.mMessage.c_str();
    EXPECT_EQ(CorruptComputeKey, FirstComputeKey);
    EXPECT_EQ(CorruptGraphicsKey, FirstGraphicsKey);
    ShutdownBackend();

    std::filesystem::remove_all(Directory, Error);
}
#endif

TEST(ArdaPipelineStateCache, PersistsAndReloadsVulkanBlobsWhenAvailable)
{
    using namespace arda::backend;

    ShutdownBackend();
    const auto Unique = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::filesystem::path Directory =
        std::filesystem::temp_directory_path() /
        ("arda-vulkan-pso-cache-" + std::to_string(Unique));
    std::error_code Error;
    std::filesystem::remove_all(Directory, Error);

    FArdaBackendConfiguration Configuration;
    FCapturingDiagnosticCallback Diagnostics;
    Configuration.mBackendName = "native-vulkan";
    Configuration.mbEnableValidation = false;
    Configuration.mPipelineCacheDirectory = Directory;
    Configuration.mMessageCallback = &Diagnostics;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    if (!InitializeBackend())
    {
        std::filesystem::remove_all(Directory, Error);
        GTEST_SKIP() << GetBackendError().c_str();
    }
    if (!GetDevice()->GetCapabilities().mbPipelineCachePersistence)
    {
        ShutdownBackend();
        std::filesystem::remove_all(Directory, Error);
        GTEST_SKIP() << "The selected backend does not expose native pipeline-cache persistence.";
    }

    uint64_t FirstComputeKey = 0;
    uint64_t FirstGraphicsKey = 0;
    uint64_t FirstMeshletKey = 0;
    const auto FirstCreateStatus = CreatePersistentTestPipelines(
        "native-vulkan", FirstComputeKey, FirstGraphicsKey,
        &FirstMeshletKey);
    ASSERT_TRUE(FirstCreateStatus)
        << FirstCreateStatus.mMessage.c_str();
    ShutdownBackend();

    const auto CacheFile = Directory / "native-vulkan.pso-cache";
    ASSERT_TRUE(std::filesystem::is_regular_file(CacheFile));
    EXPECT_GT(std::filesystem::file_size(CacheFile), 24u);

    Diagnostics.Clear();
    ASSERT_TRUE(ConfigureBackend(Configuration));
    ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
    EXPECT_TRUE(Diagnostics.Contains(
        "Vulkan persistent pipeline cache data was accepted"));
    uint64_t ReloadedComputeKey = 0;
    uint64_t ReloadedGraphicsKey = 0;
    uint64_t ReloadedMeshletKey = 0;
    const auto ReloadedCreateStatus = CreatePersistentTestPipelines(
        "native-vulkan",
        ReloadedComputeKey,
        ReloadedGraphicsKey,
        &ReloadedMeshletKey);
    EXPECT_TRUE(ReloadedCreateStatus)
        << ReloadedCreateStatus.mMessage.c_str();
    EXPECT_EQ(ReloadedComputeKey, FirstComputeKey);
    EXPECT_EQ(ReloadedGraphicsKey, FirstGraphicsKey);
    EXPECT_EQ(ReloadedMeshletKey, FirstMeshletKey);
    ShutdownBackend();

    std::filesystem::remove_all(Directory, Error);
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
            *Device, GetBackendConfiguration().mBackendName.c_str(), "ArdaShaderStructTest",
            "ShaderStructTestCS", EArdaRHIShaderStage::Compute);
        auto ShaderB = CreateTestShader(
            *Device, GetBackendConfiguration().mBackendName.c_str(), "ArdaShaderStructTest",
            "ShaderStructTestCS", EArdaRHIShaderStage::Compute);
        auto ShaderC = CreateTestShader(
            *Device, GetBackendConfiguration().mBackendName.c_str(), "ArdaShaderStructTest",
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
            *Device, GetBackendConfiguration().mBackendName.c_str(), "ArdaPipelineStateTestVS",
            "PipelineStateTestVS", EArdaRHIShaderStage::Vertex);
        auto PixelShader = CreateTestShader(
            *Device, GetBackendConfiguration().mBackendName.c_str(), "ArdaPipelineStateTestPS",
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

TEST(ArdaPipelineStateCache, CachesPrecachesBindsAndEvictsMeshletPipelines)
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

    FArdaRHIDeviceRef Device = GetDevice();
    ASSERT_TRUE(Device);
    if (Device->GetCapabilities().mMeshShaderTier ==
        EArdaRHIMeshShaderTier::None)
    {
        ShutdownBackend();
        GTEST_SKIP() << "The selected device does not support mesh shaders.";
    }

    {
        auto MeshShader = CreateTestShader(
            *Device, GetBackendConfiguration().mBackendName.c_str(),
            "ArdaMeshPipelineTestMS", "MeshPipelineTestMS",
            EArdaRHIShaderStage::Mesh);
        auto PixelShader = CreateTestShader(
            *Device, GetBackendConfiguration().mBackendName.c_str(),
            "ArdaMeshPipelineTestPS", "MeshPipelineTestPS",
            EArdaRHIShaderStage::Pixel);
        ASSERT_TRUE(MeshShader) << MeshShader.mStatus.mMessage.c_str();
        ASSERT_TRUE(PixelShader) << PixelShader.mStatus.mMessage.c_str();

        FArdaRHITextureDesc TargetDesc;
        TargetDesc.mWidth = 4;
        TargetDesc.mHeight = 4;
        TargetDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
        TargetDesc.mUsage = EArdaRHITextureUsage::RenderTarget;
        TargetDesc.mInitialState = EArdaRHIResourceState::RenderTarget;
        TargetDesc.mbKeepInitialState = true;
        auto Target = Device->CreateTexture(TargetDesc);
        ASSERT_TRUE(Target) << Target.mStatus.mMessage.c_str();
        FArdaRHIFramebufferDesc FramebufferDesc;
        FramebufferDesc.mColorAttachments.push_back({ Target.mValue, {} });
        auto Framebuffer = Device->CreateFramebuffer(FramebufferDesc);
        ASSERT_TRUE(Framebuffer) << Framebuffer.mStatus.mMessage.c_str();

        FArdaMeshletPipelineStateInitializer Initializer;
        Initializer.mDesc.mMeshShader = MeshShader.mValue;
        Initializer.mDesc.mPixelShader = PixelShader.mValue;
        Initializer.mDesc.mRasterState.mCullMode = EArdaRHICullMode::None;
        Initializer.mDesc.mDepthStencilState.mbDepthTest = false;
        Initializer.mDesc.mDepthStencilState.mbDepthWrite = false;
        Initializer.mDesc.mDebugName = "Cached meshlet";

        FArdaPipelineStateCacheConfiguration Configuration;
        Configuration.mMaxMeshletEntries = 2;
        FArdaPipelineStateCache Cache(Device, Configuration);
        ASSERT_TRUE(Cache.PrecacheMeshlet(Initializer, Framebuffer.mValue));
        EXPECT_EQ(Cache.GetStats().mMisses, 1u);
        EXPECT_EQ(Cache.GetStats().mMeshletEntries, 1u);

        FArdaRHIMeshletPipelineRef First;
        ASSERT_TRUE(Cache.GetOrCreateMeshlet(
            Initializer, Framebuffer.mValue, First));
        ASSERT_TRUE(First);
        EXPECT_EQ(Cache.GetStats().mHits, 1u);
        ASSERT_EQ(First->GetDesc().mColorFormats.size(), 1u);
        EXPECT_EQ(
            First->GetDesc().mColorFormats[0],
            EArdaRHIFormat::RGBA8UNorm);
        EXPECT_EQ(First->GetDesc().mSampleCount, 1u);
        EXPECT_NE(First->GetDesc().mPersistentCacheKey, 0u);

        auto Relabeled = Initializer;
        Relabeled.mDesc.mDebugName = "Another meshlet label";
        FArdaRHIMeshletPipelineRef Reused;
        ASSERT_TRUE(Cache.GetOrCreateMeshlet(
            Relabeled, Framebuffer.mValue, Reused));
        EXPECT_EQ(Reused.Get(), First.Get());

        auto CommandList =
            Device->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(CommandList);
        ASSERT_TRUE(CommandList.mValue->Open());
        FArdaRHIMeshletState State;
        State.mFramebuffer = Framebuffer.mValue;
        State.mViewports.push_back({ 0.f, 4.f, 0.f, 4.f, 0.f, 1.f });
        State.mScissors.push_back({ 0, 4, 0, 4 });
        EXPECT_TRUE(Cache.SetMeshletPipelineState(
            *CommandList.mValue, Initializer, eastl::move(State)));
        EXPECT_TRUE(CommandList.mValue->Close());

        Cache.Trim(128, 128, 0);
        EXPECT_EQ(Cache.GetStats().mMeshletEntries, 0u);
        FArdaRHIMeshletPipelineRef AfterTrim;
        ASSERT_TRUE(Cache.GetOrCreateMeshlet(
            Initializer, Framebuffer.mValue, AfterTrim));
        EXPECT_NE(AfterTrim.Get(), First.Get());

        auto Mismatched = Initializer;
        Mismatched.mDesc.mColorFormats.push_back(
            EArdaRHIFormat::BGRA8UNorm);
        EXPECT_EQ(
            Cache.GetOrCreateMeshlet(
                Mismatched, Framebuffer.mValue, Reused).mCode,
            EArdaRHIResult::InvalidArgument);

        Cache.Clear();
        EXPECT_EQ(Cache.GetStats().mMeshletEntries, 0u);
    }

    Device.Reset();
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
                *Device, GetBackendConfiguration().mBackendName.c_str(),
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
        ASSERT_NE(A->GetDesc().mPersistentCacheKey, 0u);
        EXPECT_EQ(
            A->GetDesc().mPersistentCacheKey,
            BRecreated->GetDesc().mPersistentCacheKey);

        FArdaRHIBindingLayoutDesc ChangedLayoutDesc = LayoutDesc;
        ChangedLayoutDesc.mItems.push_back(
            { 1, 1, EArdaRHIBindingType::StructuredBufferUAV });
        auto ChangedLayout = Device->CreateBindingLayout(ChangedLayoutDesc);
        ASSERT_TRUE(ChangedLayout);
        FArdaComputePipelineStateInitializer Changed = Initializers[0];
        Changed.mDesc.mBindingLayouts[0] = ChangedLayout.mValue;
        FArdaRHIComputePipelineRef ChangedPipeline;
        ASSERT_TRUE(Cache.GetOrCreateCompute(Changed, ChangedPipeline));
        EXPECT_NE(
            A->GetDesc().mPersistentCacheKey,
            ChangedPipeline->GetDesc().mPersistentCacheKey);
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
            *Device, GetBackendConfiguration().mBackendName.c_str(),
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
