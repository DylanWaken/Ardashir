#include "ArdaBackend.h"

#include <gtest/gtest.h>

#include <fstream>
#include <vector>

#if defined(_WIN32) && defined(ARDA_TEST_NVRHI_D3D12)
#include <d3d12.h>
#include <wrl/client.h>
#endif

#ifndef ARDA_BACKEND_TEST_SHADER_DIR
#define ARDA_BACKEND_TEST_SHADER_DIR "."
#endif

namespace
{
    arda::backend::IArdaBackendModule* FindLinkedTestBackendModule()
    {
        using namespace arda::backend;
        if (IArdaBackendModule* Module =
                FindDefaultBackendModule(DefaultBackend))
        {
            return Module;
        }
        const auto Modules = EnumerateBackendModules();
        return Modules.empty()
            ? nullptr
            : FindBackendModule(Modules.front().mName.c_str());
    }

    class FExternalTestCleanup
    {
    public:
        ~FExternalTestCleanup()
        {
            arda::backend::ShutdownBackend();
            for (auto It = mResourceProviders.rbegin();
                It != mResourceProviders.rend(); ++It)
            {
                static_cast<void>(
                    arda::backend::UnregisterExternalResourceProvider(**It));
            }
            for (auto It = mDeviceProviders.rbegin();
                It != mDeviceProviders.rend(); ++It)
            {
                static_cast<void>(
                    arda::backend::UnregisterExternalDeviceProvider(**It));
            }
        }

        bool Register(arda::backend::IArdaExternalDeviceProvider& Provider)
        {
            if (!arda::backend::RegisterExternalDeviceProvider(Provider))
                return false;
            for (const auto* Existing : mDeviceProviders)
                if (Existing == &Provider)
                    return true;
            mDeviceProviders.push_back(&Provider);
            return true;
        }

        bool Register(arda::backend::IArdaExternalResourceProvider& Provider)
        {
            if (!arda::backend::RegisterExternalResourceProvider(Provider))
                return false;
            for (const auto* Existing : mResourceProviders)
                if (Existing == &Provider)
                    return true;
            mResourceProviders.push_back(&Provider);
            return true;
        }

    private:
        std::vector<arda::backend::IArdaExternalDeviceProvider*> mDeviceProviders;
        std::vector<arda::backend::IArdaExternalResourceProvider*> mResourceProviders;
    };

    class FTestDeviceProvider final
        : public arda::backend::IArdaExternalDeviceProvider
    {
    public:
        arda::backend::EArdaBackendType mBackend =
            arda::backend::DefaultBackend;
        eastl::shared_ptr<void> mToken;
#if defined(_WIN32)
        arda::backend::FArdaExternalDeviceDesc mExternal;
        bool mbSupplyD3D12 = false;
#endif

        arda::backend::EArdaBackendType GetBackendType() const noexcept override
        {
            return mBackend;
        }
#if defined(_WIN32)
        bool GetExternalDeviceDesc(
            arda::backend::FArdaExternalDeviceDesc& OutDesc) const override
        {
            OutDesc = mExternal;
            return mbSupplyD3D12;
        }
#endif
        eastl::shared_ptr<void> GetLifetimeToken() const override
        {
            return mToken;
        }
    };

    class FTestResourceProvider final
        : public arda::backend::IArdaExternalResourceProvider
    {
    public:
        const char* mName = "test.resources";
        arda::backend::EArdaBackendType mBackend =
            arda::backend::DefaultBackend;
        arda::rhi::FArdaRHIStatus mTextureStatus =
            arda::rhi::FArdaRHIStatus::Success();
        arda::rhi::FArdaRHIStatus mBufferStatus =
            arda::rhi::FArdaRHIStatus::Success();
        arda::rhi::FArdaRHINativeTextureImportDesc mTexture;
        arda::rhi::FArdaRHINativeBufferImportDesc mBuffer;
        uint64_t mLastTextureId = 0;
        uint64_t mLastBufferId = 0;

        const char* GetName() const noexcept override { return mName; }
        arda::backend::EArdaBackendType GetBackendType() const noexcept override
        {
            return mBackend;
        }
        arda::rhi::FArdaRHIStatus ResolveNativeTexture(
            uint64_t Id,
            arda::rhi::FArdaRHINativeTextureImportDesc& OutDesc) override
        {
            mLastTextureId = Id;
            OutDesc = mTexture;
            return mTextureStatus;
        }
        arda::rhi::FArdaRHIStatus ResolveNativeBuffer(
            uint64_t Id,
            arda::rhi::FArdaRHINativeBufferImportDesc& OutDesc) override
        {
            mLastBufferId = Id;
            OutDesc = mBuffer;
            return mBufferStatus;
        }
    };

    class FTestBackendModule final : public arda::backend::IArdaBackendModule
    {
    public:
        explicit FTestBackendModule(const char* Name)
        {
            mDescriptor.mName = Name;
            mDescriptor.mDisplayName = "Test backend";
            mDescriptor.mBackendType = arda::backend::EArdaBackendType::Custom;
            mDescriptor.mShaderBinaryFormat =
                arda::backend::EArdaShaderBinaryFormat::BackendDefined;
            mDescriptor.mShaderArtifactExtension = ".testbin";
            mDescriptor.mbSupportsOwnedDevice = true;
        }

        const arda::backend::FArdaBackendModuleDescriptor&
        GetDescriptor() const noexcept override
        {
            return mDescriptor;
        }

        eastl::unique_ptr<arda::backend::IArdaBackendDevice> CreateDevice(
            arda::backend::EArdaDeviceSource) override
        {
            return {};
        }

        arda::rhi::FArdaRHIStatus ConfigureShaderCompileInvocation(
            arda::backend::FArdaBackendShaderCompileInvocation&) const override
        {
            return arda::rhi::FArdaRHIStatus::Success();
        }

    private:
        arda::backend::FArdaBackendModuleDescriptor mDescriptor;
    };
}

TEST(ArdaBackend, LinkableBackendRegistrySelectsStableNamedModules)
{
    using namespace arda::backend;
    ShutdownBackend();
    const FArdaBackendConfiguration Original = GetBackendConfiguration();
    FTestBackendModule Module("test-custom-rhi");
    FTestBackendModule Collision("test-custom-rhi");

    ASSERT_TRUE(RegisterBackendModule(Module));
    EXPECT_TRUE(RegisterBackendModule(Module));
    EXPECT_FALSE(RegisterBackendModule(Collision));
    EXPECT_EQ(FindBackendModule("test-custom-rhi"), &Module);
    EXPECT_EQ(FindDefaultBackendModule(EArdaBackendType::Custom), &Module);

    const auto Modules = EnumerateBackendModules();
    const auto Position = eastl::find_if(
        Modules.begin(), Modules.end(), [](const auto& Descriptor)
        {
            return Descriptor.mName == "test-custom-rhi";
        });
    ASSERT_NE(Position, Modules.end());
    EXPECT_EQ(Position->mShaderBinaryFormat,
        EArdaShaderBinaryFormat::BackendDefined);

    ASSERT_TRUE(ConfigureBackend("test-custom-rhi"));
    EXPECT_EQ(GetBackendConfiguration().mBackendName, "test-custom-rhi");
    EXPECT_EQ(GetBackendConfiguration().mBackend, EArdaBackendType::Custom);

    ASSERT_TRUE(ConfigureBackend(Original));
    EXPECT_TRUE(UnregisterBackendModule(Module));
    EXPECT_EQ(FindBackendModule("test-custom-rhi"), nullptr);
}

TEST(ArdaBackend, NvrhiApisAreRegisteredAsSeparateBackendModules)
{
    using namespace arda::backend;
    IArdaBackendModule* Vulkan = FindBackendModule("nvrhi-vulkan");
#if defined(ARDA_TEST_NVRHI_VULKAN)
    ASSERT_NE(Vulkan, nullptr);
    EXPECT_EQ(Vulkan->GetDescriptor().mBackendType, EArdaBackendType::Vulkan);
    EXPECT_EQ(Vulkan->GetDescriptor().mShaderArtifactExtension, ".spv");
#else
    EXPECT_EQ(Vulkan, nullptr);
#endif
    IArdaBackendModule* D3D12 = FindBackendModule("nvrhi-d3d12");
#if defined(ARDA_TEST_NVRHI_D3D12)
    ASSERT_NE(D3D12, nullptr);
    if (Vulkan)
        EXPECT_NE(D3D12, Vulkan);
    EXPECT_EQ(D3D12->GetDescriptor().mBackendType, EArdaBackendType::D3D12);
    EXPECT_EQ(D3D12->GetDescriptor().mShaderArtifactExtension, ".dxil");
#else
    EXPECT_EQ(D3D12, nullptr);
#endif
}

TEST(ArdaBackend, ExternalDeviceProviderRegistrationIsDeterministic)
{
    using namespace arda::backend;
    ShutdownBackend();
    FTestDeviceProvider First;
    FTestDeviceProvider Collision;
    FExternalTestCleanup Cleanup;

    EXPECT_EQ(GetExternalDeviceProvider(), nullptr);
    ASSERT_TRUE(Cleanup.Register(First));
    EXPECT_TRUE(RegisterExternalDeviceProvider(First));
    EXPECT_EQ(GetExternalDeviceProvider(), &First);
    EXPECT_FALSE(RegisterExternalDeviceProvider(Collision));
    EXPECT_NE(GetBackendError().find("different"), eastl::string::npos);
    EXPECT_FALSE(UnregisterExternalDeviceProvider(Collision));
    EXPECT_EQ(GetExternalDeviceProvider(), &First);
    EXPECT_TRUE(UnregisterExternalDeviceProvider(First));
    EXPECT_EQ(GetExternalDeviceProvider(), nullptr);
    EXPECT_TRUE(UnregisterExternalDeviceProvider(First));
}

TEST(ArdaBackend, ExternalDeviceSourceReportsMissingAndMismatchedProviders)
{
    using namespace arda::backend;
    ShutdownBackend();
    FTestDeviceProvider Provider;
    FExternalTestCleanup Cleanup;
    IArdaBackendModule* Module = FindLinkedTestBackendModule();
    ASSERT_NE(Module, nullptr);
    const EArdaBackendType TestBackend = Module->GetDescriptor().mBackendType;
    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = Module->GetDescriptor().mName;
    Configuration.mBackend = TestBackend;
    Configuration.mDeviceSource = EArdaDeviceSource::ExternalProvider;
    Configuration.mbEnableValidation = false;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    EXPECT_EQ(
        GetDeviceContext().mDeviceSource,
        EArdaDeviceSource::ExternalProvider);
    EXPECT_FALSE(InitializeBackend());
    EXPECT_NE(GetBackendError().find("registered provider"), eastl::string::npos);

    Provider.mBackend = TestBackend == EArdaBackendType::D3D12
        ? EArdaBackendType::Vulkan
        : EArdaBackendType::D3D12;
    ASSERT_TRUE(Cleanup.Register(Provider));
    EXPECT_FALSE(InitializeBackend());
    EXPECT_NE(GetBackendError().find("does not match"), eastl::string::npos);
}

TEST(ArdaBackend, NamedExternalResourceProviderRegistryIsDeterministic)
{
    using namespace arda::backend;
    ShutdownBackend();
    FTestResourceProvider Empty;
    Empty.mName = "";
    FTestResourceProvider First;
    FTestResourceProvider Collision;
    FExternalTestCleanup Cleanup;

    EXPECT_FALSE(RegisterExternalResourceProvider(Empty));
    EXPECT_NE(GetBackendError().find("non-empty"), eastl::string::npos);
    ASSERT_TRUE(Cleanup.Register(First));
    EXPECT_TRUE(RegisterExternalResourceProvider(First));
    EXPECT_EQ(GetExternalResourceProvider("test.resources"), &First);
    EXPECT_EQ(GetExternalResourceProvider(""), nullptr);
    EXPECT_FALSE(RegisterExternalResourceProvider(Collision));
    EXPECT_NE(GetBackendError().find("already registered"), eastl::string::npos);
    EXPECT_FALSE(UnregisterExternalResourceProvider(Collision));
    EXPECT_EQ(GetExternalResourceProvider("test.resources"), &First);
    EXPECT_TRUE(UnregisterExternalResourceProvider(First));
    EXPECT_EQ(GetExternalResourceProvider("test.resources"), nullptr);
    EXPECT_TRUE(UnregisterExternalResourceProvider(First));
}

TEST(ArdaBackend, NamedExternalResourceImportFailsCleanly)
{
    using namespace arda;
    using namespace backend;
    ShutdownBackend();
    FTestResourceProvider Provider;
    FExternalTestCleanup Cleanup;

    auto Missing = ImportExternalBuffer("missing.resources", 11);
    EXPECT_FALSE(Missing);
    EXPECT_EQ(Missing.mStatus.mCode, rhi::EArdaRHIResult::InvalidArgument);

    ASSERT_TRUE(Cleanup.Register(Provider));
    auto Uninitialized = ImportExternalBuffer(Provider.mName, 12);
    EXPECT_FALSE(Uninitialized);
    EXPECT_EQ(
        Uninitialized.mStatus.mCode,
        rhi::EArdaRHIResult::InvalidState);

    IArdaBackendModule* Module = FindLinkedTestBackendModule();
    ASSERT_NE(Module, nullptr);
    const EArdaBackendType TestBackend = Module->GetDescriptor().mBackendType;
    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = Module->GetDescriptor().mName;
    Configuration.mBackend = TestBackend;
    Configuration.mbEnableValidation = false;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    Provider.mBackend = TestBackend == EArdaBackendType::D3D12
        ? EArdaBackendType::Vulkan
        : EArdaBackendType::D3D12;
    auto WrongBackend = ImportExternalBuffer(Provider.mName, 13);
    EXPECT_FALSE(WrongBackend);
    EXPECT_EQ(WrongBackend.mStatus.mCode, rhi::EArdaRHIResult::WrongDevice);

    Provider.mBackend = TestBackend;
    Provider.mBufferStatus = rhi::FArdaRHIStatus::Error(
        rhi::EArdaRHIResult::BackendFailure,
        "provider-specific buffer failure");
    auto ProviderFailure = ImportExternalBuffer(Provider.mName, 14);
    EXPECT_FALSE(ProviderFailure);
    EXPECT_EQ(
        ProviderFailure.mStatus.mCode,
        rhi::EArdaRHIResult::BackendFailure);
    EXPECT_STREQ(
        ProviderFailure.mStatus.mMessage.c_str(),
        "provider-specific buffer failure");
    EXPECT_EQ(Provider.mLastBufferId, 14u);

    Provider.mTextureStatus = rhi::FArdaRHIStatus::Error(
        rhi::EArdaRHIResult::InvalidArgument,
        "provider-specific texture failure");
    auto TextureFailure = ImportExternalTexture(Provider.mName, 15);
    EXPECT_FALSE(TextureFailure);
    EXPECT_EQ(
        TextureFailure.mStatus.mCode,
        rhi::EArdaRHIResult::InvalidArgument);
    EXPECT_STREQ(
        TextureFailure.mStatus.mMessage.c_str(),
        "provider-specific texture failure");
    EXPECT_EQ(Provider.mLastTextureId, 15u);
}

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

TEST(ArdaBackend, ValidatesAndResolvesShaderCacheConfiguration)
{
    using namespace arda::backend;

    ShutdownBackend();
    FArdaBackendConfiguration Configuration;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::Startup;
    Configuration.mShaderCacheDirectory.clear();
    EXPECT_FALSE(ConfigureBackend(Configuration));
    EXPECT_NE(
        GetBackendError().find("must not be empty"),
        eastl::string::npos);

    Configuration.mShaderCompilationMode =
        EArdaShaderCompilationMode::OnDemand;
    Configuration.mShaderCacheDirectory =
        std::filesystem::path(".arda-test-cache") / "shaders";
    Configuration.mPipelineCacheDirectory =
        std::filesystem::path(".arda-test-cache") / "pipelines";
    ASSERT_TRUE(ConfigureBackend(Configuration));
    EXPECT_EQ(
        GetBackendConfiguration().mShaderCompilationMode,
        EArdaShaderCompilationMode::OnDemand);
    EXPECT_TRUE(GetBackendConfiguration().mShaderCacheDirectory.is_absolute());
    EXPECT_EQ(
        GetBackendConfiguration().mShaderCacheDirectory.filename(),
        "shaders");
    EXPECT_TRUE(GetBackendConfiguration().mPipelineCacheDirectory.is_absolute());
    EXPECT_EQ(
        GetBackendConfiguration().mPipelineCacheDirectory.filename(),
        "pipelines");

    Configuration.mPipelineCacheDirectory.clear();
    ASSERT_TRUE(ConfigureBackend(Configuration));
    EXPECT_TRUE(GetBackendConfiguration().mPipelineCacheDirectory.empty());

    const auto NonDirectoryPath =
        std::filesystem::temp_directory_path() /
        "arda-pipeline-cache-path-validation.tmp";
    std::error_code Error;
    std::filesystem::remove(NonDirectoryPath, Error);
    {
        std::ofstream File(NonDirectoryPath, std::ios::binary);
        ASSERT_TRUE(File) << NonDirectoryPath.string();
        File << "not a directory";
    }
    Configuration.mPipelineCacheDirectory = NonDirectoryPath;
    EXPECT_FALSE(ConfigureBackend(Configuration));
    EXPECT_NE(
        GetBackendError().find("not a directory"),
        eastl::string::npos);
    std::filesystem::remove(NonDirectoryPath, Error);
}

TEST(ArdaBackend, ReportsQueueAvailabilityByArdaQueueType)
{
    arda::backend::FArdaQueueCapabilities Capabilities;
    Capabilities.mbGraphics = true;
    Capabilities.mbCopy = true;

    EXPECT_TRUE(Capabilities.IsQueueAvailable(arda::rhi::EArdaRHIQueueType::Graphics));
    EXPECT_FALSE(Capabilities.IsQueueAvailable(arda::rhi::EArdaRHIQueueType::Compute));
    EXPECT_TRUE(Capabilities.IsQueueAvailable(arda::rhi::EArdaRHIQueueType::Copy));
}

TEST(ArdaBackend, EmptyOpaqueDeviceReferencesAreSafe)
{
    arda::rhi::FArdaRHIDeviceRef First;
    arda::rhi::FArdaRHIDeviceRef Second = First;
    EXPECT_FALSE(First);
    EXPECT_FALSE(Second);
}

#if defined(_WIN32) && defined(ARDA_TEST_NVRHI_D3D12)
TEST(ArdaBackend, AdoptsRealExternalD3D12DeviceAndResources)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;
    ShutdownBackend();

    Microsoft::WRL::ComPtr<ID3D12Device> NativeDevice;
    if (FAILED(D3D12CreateDevice(
            nullptr,
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&NativeDevice))))
    {
        GTEST_SKIP() << "D3D12 is unavailable.";
    }

    const auto CreateQueue = [&NativeDevice](D3D12_COMMAND_LIST_TYPE Type)
    {
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue;
        D3D12_COMMAND_QUEUE_DESC Desc{};
        Desc.Type = Type;
        NativeDevice->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Queue));
        return Queue;
    };
    auto GraphicsQueue = CreateQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto ComputeQueue = CreateQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    auto CopyQueue = CreateQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    ASSERT_TRUE(GraphicsQueue);

    FTestDeviceProvider DeviceProvider;
    DeviceProvider.mBackend = EArdaBackendType::D3D12;
    DeviceProvider.mbSupplyD3D12 = true;
    DeviceProvider.mExternal.mNativeApi = "d3d12";
    DeviceProvider.mExternal.mDevice = FArdaNativeObject(NativeDevice.Get());
    DeviceProvider.mExternal.mQueues = {
        { rhi::EArdaRHIQueueType::Graphics, FArdaNativeObject(GraphicsQueue.Get()) },
        { rhi::EArdaRHIQueueType::Compute, FArdaNativeObject(ComputeQueue.Get()) },
        { rhi::EArdaRHIQueueType::Copy, FArdaNativeObject(CopyQueue.Get()) }
    };

    FTestResourceProvider ResourceProvider;
    ResourceProvider.mBackend = EArdaBackendType::D3D12;
    FExternalTestCleanup Cleanup;

    auto Token = eastl::make_shared<int>(42);
    eastl::weak_ptr<void> WeakToken(Token);
    DeviceProvider.mToken = Token;

    D3D12_HEAP_PROPERTIES HeapProperties{};
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC BufferDesc{};
    BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    BufferDesc.Width = 4096;
    BufferDesc.Height = 1;
    BufferDesc.DepthOrArraySize = 1;
    BufferDesc.MipLevels = 1;
    BufferDesc.SampleDesc.Count = 1;
    BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> NativeBuffer;
    ASSERT_TRUE(SUCCEEDED(NativeDevice->CreateCommittedResource(
        &HeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &BufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&NativeBuffer))));

    D3D12_RESOURCE_DESC TextureDesc{};
    TextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    TextureDesc.Width = 4;
    TextureDesc.Height = 4;
    TextureDesc.DepthOrArraySize = 1;
    TextureDesc.MipLevels = 1;
    TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    Microsoft::WRL::ComPtr<ID3D12Resource> NativeTexture;
    ASSERT_TRUE(SUCCEEDED(NativeDevice->CreateCommittedResource(
        &HeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &TextureDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&NativeTexture))));

    ResourceProvider.mBuffer.mNativeObject =
        reinterpret_cast<uintptr_t>(NativeBuffer.Get());
    ResourceProvider.mBuffer.mNativeType =
        EArdaRHINativeResourceType::D3D12Resource;
    ResourceProvider.mBuffer.mInitialState = EArdaRHIResourceState::Common;
    ResourceProvider.mBuffer.mBuffer.mByteSize = 4096;
    ResourceProvider.mBuffer.mBuffer.mInitialState =
        EArdaRHIResourceState::Common;
    ResourceProvider.mBuffer.mLifetimeToken = Token;
    ResourceProvider.mTexture.mNativeObject =
        reinterpret_cast<uintptr_t>(NativeTexture.Get());
    ResourceProvider.mTexture.mNativeType =
        EArdaRHINativeResourceType::D3D12Resource;
    ResourceProvider.mTexture.mInitialState = EArdaRHIResourceState::Common;
    ResourceProvider.mTexture.mTexture.mWidth = 4;
    ResourceProvider.mTexture.mTexture.mHeight = 4;
    ResourceProvider.mTexture.mTexture.mFormat = EArdaRHIFormat::RGBA8UNorm;
    ResourceProvider.mTexture.mTexture.mInitialState =
        EArdaRHIResourceState::Common;
    ResourceProvider.mTexture.mLifetimeToken = Token;

    ASSERT_TRUE(Cleanup.Register(DeviceProvider));
    ASSERT_TRUE(Cleanup.Register(ResourceProvider));
    FArdaBackendConfiguration Configuration;
    Configuration.mBackend = EArdaBackendType::D3D12;
    Configuration.mDeviceSource = EArdaDeviceSource::ExternalProvider;
    Configuration.mbEnableValidation = false;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();

    EXPECT_EQ(GetDeviceContext().mBackend, EArdaBackendType::D3D12);
    EXPECT_EQ(
        GetDeviceContext().mDeviceSource,
        EArdaDeviceSource::ExternalProvider);
    EXPECT_TRUE(GetQueueCapabilities().mbGraphics);
    EXPECT_EQ(GetQueueCapabilities().mbCompute, ComputeQueue != nullptr);
    EXPECT_EQ(GetQueueCapabilities().mbCopy, CopyQueue != nullptr);

    FArdaRHIDeviceRef Device = GetDevice();
    FArdaRHIDeviceRef SurvivingDevice = Device;
    ASSERT_TRUE(Device);
    FArdaRHIBufferDesc ArdaBufferDesc;
    ArdaBufferDesc.mByteSize = 256;
    EXPECT_TRUE(Device->CreateBuffer(ArdaBufferDesc));
    auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
    ASSERT_TRUE(Commands);
    EXPECT_TRUE(Commands.mValue->Open());
    EXPECT_TRUE(Commands.mValue->Close());

    auto FirstBuffer = ImportExternalBuffer(ResourceProvider.mName, 101);
    auto SecondBuffer = ImportExternalBuffer(ResourceProvider.mName, 101);
    auto FirstTexture = ImportExternalTexture(ResourceProvider.mName, 202);
    auto SecondTexture = ImportExternalTexture(ResourceProvider.mName, 202);
    ASSERT_TRUE(FirstBuffer);
    ASSERT_TRUE(SecondBuffer);
    ASSERT_TRUE(FirstTexture);
    ASSERT_TRUE(SecondTexture);
    EXPECT_EQ(FirstBuffer.mValue.Get(), SecondBuffer.mValue.Get());
    EXPECT_EQ(FirstTexture.mValue.Get(), SecondTexture.mValue.Get());
    EXPECT_EQ(ResourceProvider.mLastBufferId, 101u);
    EXPECT_EQ(ResourceProvider.mLastTextureId, 202u);

    Token.reset();
    DeviceProvider.mToken.reset();
    ResourceProvider.mBuffer.mLifetimeToken.reset();
    ResourceProvider.mTexture.mLifetimeToken.reset();
    EXPECT_FALSE(WeakToken.expired());

    FirstBuffer.mValue = nullptr;
    SecondBuffer.mValue = nullptr;
    FirstTexture.mValue = nullptr;
    SecondTexture.mValue = nullptr;
    Commands.mValue = nullptr;
    Device = nullptr;
    ShutdownBackend();
    EXPECT_FALSE(WeakToken.expired());
    EXPECT_EQ(NativeDevice->GetNodeCount(), 1u);
    EXPECT_EQ(
        GraphicsQueue->GetDesc().Type,
        D3D12_COMMAND_LIST_TYPE_DIRECT);

    SurvivingDevice = nullptr;
    EXPECT_TRUE(WeakToken.expired());
}

TEST(ArdaBackend, InvalidExternalD3D12DescriptorsReportErrors)
{
    using namespace arda::backend;
    ShutdownBackend();
    Microsoft::WRL::ComPtr<ID3D12Device> NativeDevice;
    if (FAILED(D3D12CreateDevice(
            nullptr,
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&NativeDevice))))
    {
        GTEST_SKIP() << "D3D12 is unavailable.";
    }

    D3D12_COMMAND_QUEUE_DESC QueueDesc{};
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> CopyQueue;
    ASSERT_TRUE(SUCCEEDED(NativeDevice->CreateCommandQueue(
        &QueueDesc, IID_PPV_ARGS(&CopyQueue))));

    FTestDeviceProvider Provider;
    FExternalTestCleanup Cleanup;
    Provider.mBackend = EArdaBackendType::D3D12;
    Provider.mbSupplyD3D12 = true;
    Provider.mExternal.mNativeApi = "d3d12";
    Provider.mExternal.mDevice = FArdaNativeObject(NativeDevice.Get());
    ASSERT_TRUE(Cleanup.Register(Provider));

    FArdaBackendConfiguration Configuration;
    Configuration.mBackend = EArdaBackendType::D3D12;
    Configuration.mDeviceSource = EArdaDeviceSource::ExternalProvider;
    Configuration.mbEnableValidation = false;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    EXPECT_FALSE(InitializeBackend());
    EXPECT_NE(
        GetBackendError().find("device and graphics queue"),
        eastl::string::npos);

    ASSERT_TRUE(UnregisterExternalDeviceProvider(Provider));
    Provider.mExternal.mQueues = {
        { arda::rhi::EArdaRHIQueueType::Graphics, FArdaNativeObject(CopyQueue.Get()) }
    };
    ASSERT_TRUE(RegisterExternalDeviceProvider(Provider));
    const bool bInitialized = InitializeBackend();
    EXPECT_FALSE(bInitialized);
    EXPECT_FALSE(GetBackendError().empty());
    if (bInitialized)
        ShutdownBackend();
}

TEST(ArdaBackend, BorrowedD3D12TextureImportDeduplicatesAndReleases)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    ShutdownBackend();
    FArdaBackendConfiguration Configuration;
    Configuration.mBackend = EArdaBackendType::D3D12;
    Configuration.mbEnableValidation = false;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    Microsoft::WRL::ComPtr<ID3D12Device> NativeDevice;
    if (FAILED(D3D12CreateDevice(
            nullptr,
            D3D_FEATURE_LEVEL_12_0,
            IID_PPV_ARGS(&NativeDevice))))
    {
        ShutdownBackend();
        GTEST_SKIP() << "No standalone D3D12 test device is available.";
    }

    D3D12_HEAP_PROPERTIES HeapProperties{};
    HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC ResourceDesc{};
    ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    ResourceDesc.Width = 4;
    ResourceDesc.Height = 4;
    ResourceDesc.DepthOrArraySize = 1;
    ResourceDesc.MipLevels = 1;
    ResourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ResourceDesc.SampleDesc.Count = 1;
    ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    Microsoft::WRL::ComPtr<ID3D12Resource> NativeTexture;
    ASSERT_TRUE(SUCCEEDED(NativeDevice->CreateCommittedResource(
        &HeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &ResourceDesc,
        D3D12_RESOURCE_STATE_PRESENT,
        nullptr,
        IID_PPV_ARGS(&NativeTexture))));

    const auto RefCount = [&NativeTexture]()
    {
        const ULONG Count = NativeTexture->AddRef();
        NativeTexture->Release();
        return Count - 1;
    };
    const ULONG BaselineReferences = RefCount();

    FArdaRHINativeTextureImportDesc Import;
    Import.mNativeObject =
        reinterpret_cast<uintptr_t>(NativeTexture.Get());
    Import.mNativeType = EArdaRHINativeResourceType::D3D12Resource;
    Import.mOwnership = EArdaRHINativeOwnership::Borrowed;
    Import.mInitialState = EArdaRHIResourceState::Present;
    Import.mTexture.mWidth = 4;
    Import.mTexture.mHeight = 4;
    Import.mTexture.mFormat = EArdaRHIFormat::RGBA8UNorm;
    Import.mTexture.mUsage = EArdaRHITextureUsage::RenderTarget;
    Import.mTexture.mInitialState = EArdaRHIResourceState::Present;
    Import.mTexture.mbKeepInitialState = true;
    Import.mTexture.mDebugName = "BorrowedIntegrationTexture";

    FArdaRHIDeviceRef Device = GetDevice();
    auto First = Device->ImportNativeTexture(Import);
    auto Second = Device->ImportNativeTexture(Import);
    ASSERT_TRUE(First);
    ASSERT_TRUE(Second);
    EXPECT_EQ(First.mValue.Get(), Second.mValue.Get());
    EXPECT_EQ(
        First.mValue->GetPhysicalIdentity(),
        Second.mValue->GetPhysicalIdentity());
    EXPECT_EQ(
        First.mValue->GetDesc().mInitialState,
        EArdaRHIResourceState::Present);
    EXPECT_GT(RefCount(), BaselineReferences);

    Second.mValue = nullptr;
    Device->TrimDescriptorCaches();
    EXPECT_TRUE(First.mValue);
    EXPECT_GT(RefCount(), BaselineReferences);
    First.mValue = nullptr;
    Device->RunGarbageCollection();
    EXPECT_EQ(RefCount(), BaselineReferences);

    Device = nullptr;
    ShutdownBackend();
}
#endif

namespace
{
    std::vector<uint8_t> LoadTestBinary(const char* FileName)
    {
        const std::string Path =
            std::string(ARDA_BACKEND_TEST_SHADER_DIR) + "/" + FileName;
        std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
        if (!Stream)
            return {};
        const auto Size = Stream.tellg();
        std::vector<uint8_t> Result(static_cast<size_t>(Size));
        Stream.seekg(0);
        Stream.read(
            reinterpret_cast<char*>(Result.data()),
            static_cast<std::streamsize>(Result.size()));
        return Result;
    }

    void VerifyAdvancedResources(arda::rhi::IArdaRHIDevice& Device)
    {
        using namespace arda::rhi;

        EXPECT_EQ(
            Device.QueryWorkGraphSupport().mCode,
            EArdaRHIResult::Unsupported);
        EXPECT_EQ(
            Device.QueryShaderBundleSupport().mCode,
            EArdaRHIResult::Unsupported);

        auto Event = Device.CreateEventQuery();
        auto Timer = Device.CreateTimerQuery();
        auto Fence = Device.CreateGpuFence();
        ASSERT_TRUE(Event);
        ASSERT_TRUE(Timer);
        ASSERT_TRUE(Fence);
        ASSERT_TRUE(Device.SignalEventQuery(
            Event.mValue, EArdaRHIQueueType::Graphics));
        ASSERT_TRUE(Device.WaitEventQuery(Event.mValue));
        EXPECT_TRUE(Device.PollEventQuery(Event.mValue).mValue);
        EXPECT_TRUE(Device.ResetEventQuery(Event.mValue));
        ASSERT_TRUE(Device.SignalGpuFence(
            Fence.mValue, EArdaRHIQueueType::Graphics));
        ASSERT_TRUE(Device.WaitGpuFence(Fence.mValue));
        EXPECT_TRUE(Device.PollGpuFence(Fence.mValue).mValue);
        EXPECT_TRUE(Device.ResetGpuFence(Fence.mValue));

        FArdaRHIStagingTextureDesc StagingDesc;
        StagingDesc.mDebugName = "Readback";
        StagingDesc.mTexture.mDebugName = "Readback";
        StagingDesc.mTexture.mWidth = 4;
        StagingDesc.mTexture.mHeight = 4;
        StagingDesc.mTexture.mFormat = EArdaRHIFormat::RGBA8UNorm;
        StagingDesc.mCpuAccess = EArdaRHICpuAccess::Read;
        EXPECT_TRUE(Device.CreateStagingTexture(StagingDesc));

        const auto& Capabilities = Device.GetCapabilities();
        if (Capabilities.mbHeaps)
        {
            FArdaRHIBufferDesc BufferDesc;
            BufferDesc.mDebugName = "PlacedBuffer";
            BufferDesc.mByteSize = 4096;
            BufferDesc.mbVirtual = true;
            auto Buffer = Device.CreateBuffer(BufferDesc);
            ASSERT_TRUE(Buffer);
            auto Requirements =
                Device.GetBufferMemoryRequirements(Buffer.mValue);
            ASSERT_TRUE(Requirements);
            FArdaRHIHeapDesc HeapDesc;
            HeapDesc.mDebugName = "PlacedHeap";
            HeapDesc.mCapacity = Requirements.mValue.mSize;
            auto Heap = Device.CreateHeap(HeapDesc);
            ASSERT_TRUE(Heap);
            EXPECT_TRUE(
                Device.BindBufferMemory(Buffer.mValue, Heap.mValue, 0));
        }

        if (Capabilities.mbBindless)
        {
            FArdaRHIBindlessLayoutDesc LayoutDesc;
            LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
            LayoutDesc.mMaxCapacity = 16;
            LayoutDesc.mbAllowUnsafeDescriptorTableLifetime = true;
            LayoutDesc.mRegisterSpaces.push_back(
                {0, 1, EArdaRHIBindingType::TextureSRV});
            auto Layout = Device.CreateBindlessLayout(LayoutDesc);
            ASSERT_TRUE(Layout);
            auto Table = Device.CreateDescriptorTable(Layout.mValue);
            ASSERT_TRUE(Table);
            EXPECT_TRUE(Device.ResizeDescriptorTable(Table.mValue, 8));
            EXPECT_GE(Table.mValue->GetCapacity(), 8u);
        }

        if (Capabilities.mbRayTracingAccelStruct)
        {
            FArdaRHIAccelStructDesc Desc;
            Desc.mDebugName = "EmptyTLAS";
            Desc.mbTopLevel = true;
            Desc.mTopLevelMaxInstances = 1;
            EXPECT_TRUE(Device.CreateAccelStruct(Desc));
        }

        if (Capabilities.mbRayTracing)
        {
            const bool bD3D12 =
                arda::backend::gCurrentBackend ==
                arda::backend::EArdaBackendType::D3D12;
            const auto Bytecode = LoadTestBinary(
                bD3D12
                    ? "ArdaRayTracingTest.dxil"
                    : "ArdaRayTracingTest.spv");
            ASSERT_FALSE(Bytecode.empty());

            FArdaRHIShaderRef RayGenerationShader;
            if (bD3D12)
            {
                auto Library = Device.CreateShaderLibrary(
                    Bytecode.data(),
                    Bytecode.size(),
                    "Ray-tracing cache test library");
                ASSERT_TRUE(Library);
                auto Shader = Device.GetShaderFromLibrary(
                    Library.mValue,
                    "RayGen",
                    EArdaRHIShaderStage::RayGeneration,
                    "RayGen");
                ASSERT_TRUE(Shader);
                RayGenerationShader = eastl::move(Shader.mValue);
            }
            else
            {
                FArdaRHIShaderDesc ShaderDesc;
                ShaderDesc.mStage = EArdaRHIShaderStage::RayGeneration;
                ShaderDesc.mBytecode = Bytecode.data();
                ShaderDesc.mBytecodeSize = Bytecode.size();
                ShaderDesc.mEntryPoint = "RayGen";
                ShaderDesc.mDebugName = "RayGen";
                auto Shader = Device.CreateShader(ShaderDesc);
                ASSERT_TRUE(Shader);
                RayGenerationShader = eastl::move(Shader.mValue);
            }

            FArdaRHIRayTracingPipelineDesc PipelineDesc;
            PipelineDesc.mMaxPayloadSize = 4;
            PipelineDesc.mShaders.push_back(
                { "RayGen", RayGenerationShader, {} });
            const auto SameKeyDifferentLabel = [&PipelineDesc]()
            {
                auto Copy = PipelineDesc;
                Copy.mDebugName = "Diagnostic-only label";
                return Copy;
            }();
            EXPECT_EQ(PipelineDesc, SameKeyDifferentLabel);
            EXPECT_EQ(
                HashValue(PipelineDesc),
                HashValue(SameKeyDifferentLabel));

            auto First = Device.CreateRayTracingPipeline(PipelineDesc);
            auto Reused =
                Device.CreateRayTracingPipeline(SameKeyDifferentLabel);
            ASSERT_TRUE(First);
            ASSERT_TRUE(Reused);
            EXPECT_EQ(First.mValue.Get(), Reused.mValue.Get());

            for (uint32_t Index = 1; Index <= 64; ++Index)
            {
                auto Unique = PipelineDesc;
                Unique.mMaxPayloadSize = 4 + Index * 4;
                ASSERT_TRUE(Device.CreateRayTracingPipeline(Unique));
            }
            EXPECT_LE(
                Device.GetDescriptorCacheStats().mRayTracingPipelines,
                64u);
            auto Recreated =
                Device.CreateRayTracingPipeline(PipelineDesc);
            ASSERT_TRUE(Recreated);
            EXPECT_NE(First.mValue.Get(), Recreated.mValue.Get());
            EXPECT_TRUE(First.mValue);

            Device.TrimDescriptorCaches();
            EXPECT_EQ(
                Device.GetDescriptorCacheStats().mRayTracingPipelines,
                0u);
            EXPECT_TRUE(First.mValue);
        }
    }

    void VerifyDeviceInitialization(
        arda::backend::EArdaBackendType Backend,
        bool bRequireComputeAndCopy)
    {
        using namespace arda::backend;

        ShutdownBackend();
        ASSERT_TRUE(ConfigureBackend(Backend));
        if (!InitializeBackend())
        {
            GTEST_SKIP() << GetBackendError().c_str();
        }

        EXPECT_TRUE(IsBackendInitialized());
        EXPECT_NE(GetDevice(), nullptr);
        EXPECT_NE(GetDeviceContext().mDevice, nullptr);
        EXPECT_EQ(GetDeviceContext().mBackend, Backend);
        EXPECT_EQ(gCurrentBackend, Backend);

        arda::rhi::FArdaRHIDeviceRef Device = GetDevice();
        const auto& Capabilities = GetQueueCapabilities();
        EXPECT_EQ(
            &Capabilities,
            &GetDeviceContext().mQueueCapabilities);
        EXPECT_TRUE(Capabilities.mbGraphics);
        EXPECT_EQ(
            Capabilities.mbCompute,
            Device->GetCapabilities().mbComputeQueue);
        EXPECT_EQ(
            Capabilities.mbCopy,
            Device->GetCapabilities().mbCopyQueue);
        if (bRequireComputeAndCopy)
        {
            EXPECT_TRUE(Capabilities.mbCompute);
            EXPECT_TRUE(Capabilities.mbCopy);
        }

        arda::rhi::FArdaRHIDeviceRef SharedDevice = Device;
        EXPECT_EQ(SharedDevice.Get(), Device.Get());

        constexpr arda::rhi::EArdaRHIQueueType Queues[] = {
            arda::rhi::EArdaRHIQueueType::Graphics,
            arda::rhi::EArdaRHIQueueType::Compute,
            arda::rhi::EArdaRHIQueueType::Copy
        };
        for (const arda::rhi::EArdaRHIQueueType Queue : Queues)
        {
            if (!Capabilities.IsQueueAvailable(Queue))
            {
                continue;
            }

            auto CommandList = Device->CreateCommandList(Queue);
            ASSERT_TRUE(CommandList);
            EXPECT_TRUE(CommandList.mValue->Open());
            EXPECT_TRUE(CommandList.mValue->Close());
            auto Submission = Device->ExecuteCommandList(CommandList.mValue);
            EXPECT_TRUE(Submission);
            EXPECT_NE(Submission.mValue, 0);
            EXPECT_TRUE(Device->WaitForIdle());
            EXPECT_TRUE(CommandList.mValue->Reset());
            EXPECT_TRUE(CommandList.mValue->Close());
            EXPECT_TRUE(Device->ExecuteCommandList(CommandList.mValue));
        }
        EXPECT_TRUE(Device->WaitForIdle());
        VerifyAdvancedResources(*Device);

        ShutdownBackend();
        EXPECT_TRUE(SharedDevice);
        EXPECT_TRUE(SharedDevice->GetCapabilities().mbGraphicsQueue);
        arda::rhi::FArdaRHISamplerDesc PostShutdownSampler;
        EXPECT_TRUE(SharedDevice->CreateSampler(PostShutdownSampler));
        EXPECT_FALSE(IsBackendInitialized());
        EXPECT_FALSE(GetQueueCapabilities().mbGraphics);
        EXPECT_FALSE(GetQueueCapabilities().mbCompute);
        EXPECT_FALSE(GetQueueCapabilities().mbCopy);
    }
}

#if defined(_WIN32) && defined(ARDA_TEST_NVRHI_D3D12)
TEST(ArdaBackend, InitializesD3D12Device)
{
    VerifyDeviceInitialization(arda::backend::EArdaBackendType::D3D12, true);
}
#endif

TEST(ArdaBackend, InitializesVulkanDevice)
{
    VerifyDeviceInitialization(arda::backend::EArdaBackendType::Vulkan, false);
}
