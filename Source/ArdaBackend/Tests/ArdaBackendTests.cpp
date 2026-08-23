#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaExternalInterop.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32) && defined(ARDA_TEST_NATIVE_D3D12)
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
            // ConfigureBackend(backend/name) deliberately preserves the rest
            // of the current configuration. Restore defaults before any
            // stack-owned diagnostic callback or provider is destroyed so a
            // later test cannot inherit an expired non-owning pointer.
            static_cast<void>(arda::backend::ConfigureBackend(
                arda::backend::FArdaBackendConfiguration{}));
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

    class FCollectingDiagnosticCallback final
        : public arda::backend::IArdaDiagnosticCallback
    {
    public:
        void Message(
            arda::backend::EArdaDiagnosticSeverity Severity,
            const char*) override
        {
            if (Severity == arda::backend::EArdaDiagnosticSeverity::Error ||
                Severity == arda::backend::EArdaDiagnosticSeverity::Fatal)
                mErrors.fetch_add(1, std::memory_order_relaxed);
        }

        [[nodiscard]] uint32_t GetErrorCount() const noexcept
        {
            return mErrors.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<uint32_t> mErrors{ 0 };
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

        arda::backend::FArdaBackendDeviceCreateResult CreateDevice(
            const arda::backend::FArdaBackendConfiguration&,
            arda::backend::IArdaWindowSurface*,
            const arda::backend::IArdaExternalDeviceProvider*) override
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

TEST(ArdaBackend, NativeApisAreRegisteredAsSeparateBackendModules)
{
    using namespace arda::backend;
    IArdaBackendModule* Vulkan = FindBackendModule("native-vulkan");
#if defined(ARDA_TEST_NATIVE_VULKAN)
    ASSERT_NE(Vulkan, nullptr);
    EXPECT_EQ(Vulkan->GetDescriptor().mBackendType, EArdaBackendType::Vulkan);
    EXPECT_EQ(Vulkan->GetDescriptor().mShaderArtifactExtension, ".spv");
#else
    EXPECT_EQ(Vulkan, nullptr);
#endif
    IArdaBackendModule* D3D12 = FindBackendModule("native-d3d12");
#if defined(ARDA_TEST_NATIVE_D3D12)
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
        GetBackendConfiguration().mDeviceSource,
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

TEST(ArdaBackend, ExposesSingleProcessWideConfigurationAndDevice)
{
    using namespace arda::backend;

    ShutdownBackend();
    ASSERT_TRUE(ConfigureBackend(DefaultBackend));

    EXPECT_EQ(GetBackendConfiguration().mBackend, DefaultBackend);
    EXPECT_FALSE(IsBackendInitialized());
    EXPECT_EQ(GetDevice(), nullptr);
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
    arda::rhi::FArdaRHIQueueCapabilities Capabilities;
    Capabilities.mbGraphics = true;
    Capabilities.mbCopy = true;

    EXPECT_TRUE(Capabilities.IsSupported(arda::rhi::EArdaRHIQueueType::Graphics));
    EXPECT_FALSE(Capabilities.IsSupported(arda::rhi::EArdaRHIQueueType::Compute));
    EXPECT_TRUE(Capabilities.IsSupported(arda::rhi::EArdaRHIQueueType::Copy));
}

TEST(ArdaBackend, EmptyOpaqueDeviceReferencesAreSafe)
{
    arda::rhi::FArdaRHIDeviceRef First;
    arda::rhi::FArdaRHIDeviceRef Second = First;
    EXPECT_FALSE(First);
    EXPECT_FALSE(Second);
}

#if defined(_WIN32) && defined(ARDA_TEST_NATIVE_D3D12)
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

    EXPECT_EQ(GetBackendConfiguration().mBackend, EArdaBackendType::D3D12);
    EXPECT_EQ(
        GetBackendConfiguration().mDeviceSource,
        EArdaDeviceSource::ExternalProvider);

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

    arda::rhi::TArdaRHIResult<arda::rhi::FArdaRHIShaderRef>
    CreateArtifactShader(
        arda::rhi::IArdaRHIDevice& Device,
        arda::backend::EArdaBackendType Backend,
        const char* Artifact,
        const char* EntryPoint,
        arda::rhi::EArdaRHIShaderStage Stage)
    {
        using namespace arda;
        const eastl::string FileName = eastl::string(Artifact) +
            backend::GetShaderArtifactExtension(Backend);
        const auto Bytecode = LoadTestBinary(FileName.c_str());
        if (Bytecode.empty())
        {
            return {
                {},
                rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidState,
                    "A required backend test shader artifact is missing.")
            };
        }
        rhi::FArdaRHIShaderDesc Desc;
        Desc.mStage = Stage;
        Desc.mBytecode = Bytecode.data();
        Desc.mBytecodeSize = Bytecode.size();
        Desc.mEntryPoint = EntryPoint;
        Desc.mDebugName = Artifact;
        return Device.CreateShader(Desc);
    }

    void VerifyAdvancedResources(arda::rhi::IArdaRHIDevice& Device)
    {
        using namespace arda::rhi;

        const auto& Capabilities = Device.GetCapabilities();
        const bool bWorkGraphs =
            Capabilities.mWorkGraphTier != EArdaRHIWorkGraphTier::None;
        const bool bShaderBundles = Capabilities.mbShaderBundleDispatch;
        EXPECT_EQ(static_cast<bool>(Device.QueryWorkGraphSupport()),
            bWorkGraphs);
        EXPECT_EQ(static_cast<bool>(Device.QueryShaderBundleSupport()),
            bShaderBundles);

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

        if (Capabilities.mDescriptors.mbBindless)
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

        if (Capabilities.mRayTracing.mbAccelerationStructures)
        {
            FArdaRHIAccelStructDesc Desc;
            Desc.mDebugName = "EmptyTLAS";
            Desc.mbTopLevel = true;
            Desc.mTopLevelMaxInstances = 1;
            EXPECT_TRUE(Device.CreateAccelStruct(Desc));
        }

        if (Capabilities.mRayTracing.mbPipelineShaders)
        {
            const bool bD3D12 =
                arda::backend::GetBackendConfiguration().mBackend ==
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
            FArdaRHIBindingLayoutDesc RayLayoutDesc;
            RayLayoutDesc.mVisibility = EArdaRHIShaderStage::AllRayTracing;
            RayLayoutDesc.mItems.push_back(
                { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
            auto RayLayout = Device.CreateBindingLayout(RayLayoutDesc);
            ASSERT_TRUE(RayLayout);
            PipelineDesc.mGlobalBindingLayouts.push_back(RayLayout.mValue);
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
        EXPECT_EQ(GetBackendConfiguration().mBackend, Backend);

        arda::rhi::FArdaRHIDeviceRef Device = GetDevice();
        const auto& Capabilities = Device->GetCapabilities().mQueues;
        EXPECT_TRUE(Capabilities.mbGraphics);
        EXPECT_EQ(
            Capabilities.mbCompute,
            Device->GetCapabilities().mQueues.mbCompute);
        EXPECT_EQ(
            Capabilities.mbCopy,
            Device->GetCapabilities().mQueues.mbCopy);
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
            if (!Capabilities.IsSupported(Queue))
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
        EXPECT_TRUE(SharedDevice->GetCapabilities().mQueues.mbGraphics);
        arda::rhi::FArdaRHISamplerDesc PostShutdownSampler;
        EXPECT_TRUE(SharedDevice->CreateSampler(PostShutdownSampler));
        EXPECT_FALSE(IsBackendInitialized());
        EXPECT_EQ(GetDevice(), nullptr);
    }
}

#if defined(_WIN32) && defined(ARDA_TEST_NATIVE_D3D12)
TEST(ArdaBackend, D3D12ValidationInitializationAllowsDxgiDebugFallback)
{
    using namespace arda::backend;
    ShutdownBackend();
    FExternalTestCleanup Cleanup;
    IArdaBackendModule* Module = FindBackendModule("native-d3d12");
    ASSERT_NE(Module, nullptr);
    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = Module->GetDescriptor().mName;
    Configuration.mBackend = EArdaBackendType::D3D12;
    Configuration.mbEnableValidation = true;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
    ASSERT_TRUE(GetDevice());
}
#endif

TEST(ArdaBackend, NativeTransientResourcesAndDescriptorsReturnToBaseline)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    FExternalTestCleanup Cleanup;
    size_t TestedBackends = 0;
    for (const FArdaBackendModuleDescriptor& Module : EnumerateBackendModules())
    {
        if (Module.mBackendType != EArdaBackendType::D3D12 &&
            Module.mBackendType != EArdaBackendType::Vulkan)
            continue;
        ShutdownBackend();
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module.mName;
        Configuration.mBackend = Module.mBackendType;
        Configuration.mbEnableValidation = false;
        Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        ASSERT_TRUE(InitializeBackend())
            << Module.mName.c_str() << ": " << GetBackendError().c_str();
        ++TestedBackends;

        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        Device->TrimDescriptorCaches();
        const FArdaRHIResourceLifetimeStats Baseline =
            Device->GetResourceLifetimeStats();

        {
            FArdaRHITextureDesc TextureDesc;
            TextureDesc.mDebugName = "LifetimeTexture";
            TextureDesc.mWidth = 8;
            TextureDesc.mHeight = 8;
            TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
            TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource |
                EArdaRHITextureUsage::UnorderedAccess |
                EArdaRHITextureUsage::RenderTarget;
            auto Texture = Device->CreateTexture(TextureDesc);
            ASSERT_TRUE(Texture);
            auto TextureReference = Device->CreateTextureReference(Texture.mValue);
            ASSERT_TRUE(TextureReference);

            FArdaRHIBufferDesc BufferDesc;
            BufferDesc.mDebugName = "LifetimeBuffer";
            BufferDesc.mByteSize = 256;
            BufferDesc.mStructureStride = sizeof(uint32_t);
            BufferDesc.mUsage = EArdaRHIBufferUsage::Structured |
                EArdaRHIBufferUsage::ShaderResource |
                EArdaRHIBufferUsage::UnorderedAccess;
            auto Buffer = Device->CreateBuffer(BufferDesc);
            ASSERT_TRUE(Buffer);

            FArdaRHIUniformBufferDesc UniformDesc;
            UniformDesc.mDebugName = "LifetimeUniform";
            UniformDesc.mByteSize = 256;
            auto Uniform = Device->CreateUniformBuffer(UniformDesc, nullptr);
            ASSERT_TRUE(Uniform);

            FArdaRHIStagingTextureDesc StagingDesc;
            StagingDesc.mDebugName = "LifetimeStaging";
            StagingDesc.mTexture = TextureDesc;
            StagingDesc.mTexture.mUsage = EArdaRHITextureUsage::ShaderResource;
            StagingDesc.mCpuAccess = EArdaRHICpuAccess::Read;
            auto Staging = Device->CreateStagingTexture(StagingDesc);
            ASSERT_TRUE(Staging);

            TArdaRHIRef<IArdaRHIResource> TextureResource(Texture.mValue.Get());
            TArdaRHIRef<IArdaRHIResource> BufferResource(Buffer.mValue.Get());
            auto TextureSrv = Device->CreateShaderResourceView(TextureResource, {});
            auto TextureUav = Device->CreateUnorderedAccessView(TextureResource, {});
            auto BufferSrv = Device->CreateShaderResourceView(BufferResource, {});
            auto BufferUav = Device->CreateUnorderedAccessView(BufferResource, {});
            ASSERT_TRUE(TextureSrv);
            ASSERT_TRUE(TextureUav);
            ASSERT_TRUE(BufferSrv);
            ASSERT_TRUE(BufferUav);

            auto Sampler = Device->CreateSampler({});
            ASSERT_TRUE(Sampler);
            FArdaRHIBindingLayoutDesc BindingLayoutDesc;
            BindingLayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
            BindingLayoutDesc.mItems.push_back(
                { 0, 1, EArdaRHIBindingType::TextureUAV });
            BindingLayoutDesc.mItems.push_back(
                { 1, 1, EArdaRHIBindingType::StructuredBufferUAV });
            BindingLayoutDesc.mItems.push_back(
                { 0, 1, EArdaRHIBindingType::Sampler });
            auto BindingLayout = Device->CreateBindingLayout(BindingLayoutDesc);
            ASSERT_TRUE(BindingLayout);
            FArdaRHIBindingSetDesc BindingSetDesc;
            BindingSetDesc.mLayout = BindingLayout.mValue;
            BindingSetDesc.mItems.push_back(
                { 0, 0, EArdaRHIBindingType::TextureUAV, TextureResource, {} });
            BindingSetDesc.mItems.push_back(
                { 1, 0, EArdaRHIBindingType::StructuredBufferUAV, BufferResource, {} });
            BindingSetDesc.mItems.push_back({
                0, 0, EArdaRHIBindingType::Sampler,
                TArdaRHIRef<IArdaRHIResource>(Sampler.mValue.Get()), {} });
            auto BindingSet = Device->CreateBindingSet(BindingSetDesc);
            ASSERT_TRUE(BindingSet);

            FArdaRHIFramebufferDesc FramebufferDesc;
            FramebufferDesc.mColorAttachments.push_back({ Texture.mValue, {} });
            auto Framebuffer = Device->CreateFramebuffer(FramebufferDesc);
            ASSERT_TRUE(Framebuffer);

            const uint32_t LibraryData[] = { 1, 2, 3, 4 };
            auto Library = Device->CreateShaderLibrary(
                LibraryData, sizeof(LibraryData), "LifetimeLibrary");
            ASSERT_TRUE(Library);
            auto VertexShader = CreateArtifactShader(
                *Device, Module.mBackendType, "ArdaPipelineStateTestVS",
                "PipelineStateTestVS", EArdaRHIShaderStage::Vertex);
            auto PixelShader = CreateArtifactShader(
                *Device, Module.mBackendType, "ArdaPipelineStateTestPS",
                "PipelineStateTestPS", EArdaRHIShaderStage::Pixel);
            auto ComputeShader = CreateArtifactShader(
                *Device, Module.mBackendType, "ArdaShaderStructTest",
                "ShaderStructTestCS", EArdaRHIShaderStage::Compute);
            ASSERT_TRUE(VertexShader);
            ASSERT_TRUE(PixelShader);
            ASSERT_TRUE(ComputeShader);
            FArdaRHIVertexAttributeDesc Attribute;
            Attribute.mSemanticName = "POSITION";
            Attribute.mFormat = EArdaRHIFormat::RG32Float;
            Attribute.mElementStride = 8;
            eastl::vector<FArdaRHIVertexAttributeDesc> Attributes;
            Attributes.push_back(Attribute);
            auto InputLayout = Device->CreateInputLayout(
                Attributes, VertexShader.mValue);
            ASSERT_TRUE(InputLayout);

            FArdaRHIGraphicsPipelineDesc GraphicsDesc;
            GraphicsDesc.mVertexShader = VertexShader.mValue;
            GraphicsDesc.mPixelShader = PixelShader.mValue;
            GraphicsDesc.mInputLayout = InputLayout.mValue;
            GraphicsDesc.mColorFormats.push_back(EArdaRHIFormat::RGBA8UNorm);
            GraphicsDesc.mDepthStencilState.mbDepthTest = false;
            GraphicsDesc.mDepthStencilState.mbDepthWrite = false;
            auto GraphicsPipeline = Device->CreateGraphicsPipeline(GraphicsDesc);
            ASSERT_TRUE(GraphicsPipeline);

            FArdaRHIBindingLayoutDesc ComputeLayoutDesc;
            ComputeLayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
            ComputeLayoutDesc.mItems.push_back(
                { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
            auto ComputeLayout = Device->CreateBindingLayout(ComputeLayoutDesc);
            ASSERT_TRUE(ComputeLayout);
            FArdaRHIComputePipelineDesc ComputeDesc;
            ComputeDesc.mComputeShader = ComputeShader.mValue;
            ComputeDesc.mBindingLayouts.push_back(ComputeLayout.mValue);
            auto ComputePipeline = Device->CreateComputePipeline(ComputeDesc);
            ASSERT_TRUE(ComputePipeline);

            auto Raster = Device->CreateRasterState({});
            auto Blend = Device->CreateBlendState({});
            auto Depth = Device->CreateDepthStencilState({});
            auto Event = Device->CreateEventQuery();
            auto Timer = Device->CreateTimerQuery();
            auto Fence = Device->CreateGpuFence();
            auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
            ASSERT_TRUE(Raster);
            ASSERT_TRUE(Blend);
            ASSERT_TRUE(Depth);
            ASSERT_TRUE(Event);
            ASSERT_TRUE(Timer);
            ASSERT_TRUE(Fence);
            ASSERT_TRUE(Commands);

            const FArdaRHIResourceLifetimeStats During =
                Device->GetResourceLifetimeStats();
            EXPECT_GT(During.GetLiveResourceCount(
                EArdaRHIResourceType::BindingSet),
                Baseline.GetLiveResourceCount(EArdaRHIResourceType::BindingSet));
            if (Module.mBackendType == EArdaBackendType::D3D12)
            {
                EXPECT_GT(During.mResourceDescriptors,
                    Baseline.mResourceDescriptors);
                EXPECT_GT(During.mSamplerDescriptors,
                    Baseline.mSamplerDescriptors);
            }
            else
            {
                EXPECT_GT(During.mDescriptorSets, Baseline.mDescriptorSets);
            }
        }

        {
            FArdaRHIBufferDesc BufferDesc;
            BufferDesc.mDebugName = "DescriptorReuseBuffer";
            BufferDesc.mByteSize = 16;
            BufferDesc.mStructureStride = sizeof(uint32_t);
            BufferDesc.mUsage = EArdaRHIBufferUsage::Structured |
                EArdaRHIBufferUsage::UnorderedAccess;
            auto Buffer = Device->CreateBuffer(BufferDesc);
            auto Sampler = Device->CreateSampler({});
            ASSERT_TRUE(Buffer);
            ASSERT_TRUE(Sampler);

            FArdaRHIBindingLayoutDesc ResourceLayoutDesc;
            ResourceLayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
            ResourceLayoutDesc.mItems.push_back(
                { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
            FArdaRHIBindingLayoutDesc SamplerLayoutDesc;
            SamplerLayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
            SamplerLayoutDesc.mItems.push_back(
                { 0, 1, EArdaRHIBindingType::Sampler });
            auto ResourceLayout = Device->CreateBindingLayout(ResourceLayoutDesc);
            auto SamplerLayout = Device->CreateBindingLayout(SamplerLayoutDesc);
            ASSERT_TRUE(ResourceLayout);
            ASSERT_TRUE(SamplerLayout);

            FArdaRHIBindingSetDesc ResourceSetDesc;
            ResourceSetDesc.mLayout = ResourceLayout.mValue;
            ResourceSetDesc.mItems.push_back({
                0, 0, EArdaRHIBindingType::StructuredBufferUAV,
                TArdaRHIRef<IArdaRHIResource>(Buffer.mValue.Get()), {} });
            const uint32_t ResourceIterations =
                Module.mBackendType == EArdaBackendType::D3D12
                    ? 65568u : 8224u;
            for (uint32_t Index = 0; Index < ResourceIterations; ++Index)
            {
                auto Set = Device->CreateBindingSet(ResourceSetDesc);
                ASSERT_TRUE(Set) << Module.mName.c_str()
                    << " failed to reuse resource descriptors at iteration " << Index;
            }

            FArdaRHIBindingSetDesc SamplerSetDesc;
            SamplerSetDesc.mLayout = SamplerLayout.mValue;
            SamplerSetDesc.mItems.push_back({
                0, 0, EArdaRHIBindingType::Sampler,
                TArdaRHIRef<IArdaRHIResource>(Sampler.mValue.Get()), {} });
            for (uint32_t Index = 0; Index < 2080u; ++Index)
            {
                auto Set = Device->CreateBindingSet(SamplerSetDesc);
                ASSERT_TRUE(Set) << Module.mName.c_str()
                    << " failed to reuse sampler descriptors at iteration " << Index;
            }
        }

        ASSERT_TRUE(Device->WaitForIdle());
        Device->RunGarbageCollection();
        Device->TrimDescriptorCaches();
        const FArdaRHIResourceLifetimeStats After =
            Device->GetResourceLifetimeStats();
        for (size_t Index = 0;
             Index < static_cast<size_t>(EArdaRHIResourceType::Count);
             ++Index)
        {
            EXPECT_EQ(After.mLiveResources[Index], Baseline.mLiveResources[Index])
                << Module.mName.c_str() << " leaked RHI resource type " << Index;
        }
        EXPECT_EQ(After.mResourceDescriptors, Baseline.mResourceDescriptors);
        EXPECT_EQ(After.mSamplerDescriptors, Baseline.mSamplerDescriptors);
        EXPECT_EQ(After.mDescriptorSets, Baseline.mDescriptorSets);
        EXPECT_EQ(After.mPendingSubmissions, Baseline.mPendingSubmissions);
        Device = nullptr;
        ShutdownBackend();
    }
    EXPECT_GT(TestedBackends, 0u);
}

TEST(ArdaBackend, NativeHostDeviceCopiesSupportBlockingAndAsyncReadback)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    FExternalTestCleanup Cleanup;
    size_t TestedBackends = 0;
    for (const FArdaBackendModuleDescriptor& Module : EnumerateBackendModules())
    {
        if (Module.mBackendType != EArdaBackendType::D3D12 &&
            Module.mBackendType != EArdaBackendType::Vulkan)
            continue;
        ShutdownBackend();
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module.mName;
        Configuration.mBackend = Module.mBackendType;
        Configuration.mbEnableValidation = false;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        ASSERT_TRUE(InitializeBackend())
            << Module.mName.c_str() << ": " << GetBackendError().c_str();
        ++TestedBackends;

        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        const auto Baseline = Device->GetResourceLifetimeStats();
        FArdaRHIBufferDesc BufferDesc;
        BufferDesc.mDebugName = "Host/device copy test";
        BufferDesc.mByteSize = 64;
        BufferDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Buffer = Device->CreateBuffer(BufferDesc);
        ASSERT_TRUE(Buffer);

        eastl::vector<uint8_t> Expected(BufferDesc.mByteSize);
        for (size_t Index = 0; Index < Expected.size(); ++Index)
            Expected[Index] = static_cast<uint8_t>(Index * 13u + 7u);
        eastl::vector<uint8_t> BlockingReadback;
        auto BlockingCommands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(BlockingCommands);
        ASSERT_TRUE(BlockingCommands.mValue->Open());
        ASSERT_TRUE(BlockingCommands.mValue->CopyBufferHostToDevice(
            *Buffer.mValue, Expected.data(), Expected.size()));
        ASSERT_TRUE(BlockingCommands.mValue->CopyBufferDeviceToHost(
            *Buffer.mValue, BlockingReadback));
        ASSERT_TRUE(BlockingCommands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(BlockingCommands.mValue));
        EXPECT_EQ(BlockingReadback, Expected) << Module.mName.c_str();

        for (size_t Index = 0; Index < Expected.size(); ++Index)
            Expected[Index] = static_cast<uint8_t>(Index * 29u + 3u);
        std::mutex CallbackMutex;
        std::condition_variable CallbackCondition;
        uint32_t CallbackCount = 0;
        FArdaRHIStatus UploadStatus;
        FArdaRHIBufferReadbackResult AsyncReadback;
        std::thread::id UploadThread;
        std::thread::id ReadbackThread;
        const std::thread::id CallingThread = std::this_thread::get_id();

        auto AsyncCommands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(AsyncCommands);
        ASSERT_TRUE(AsyncCommands.mValue->Open());
        ASSERT_TRUE(AsyncCommands.mValue->CopyBufferHostToDeviceAsync(
            *Buffer.mValue, Expected.data(), Expected.size(),
            [&](FArdaRHIStatus Status)
            {
                std::lock_guard<std::mutex> Lock(CallbackMutex);
                UploadStatus = eastl::move(Status);
                UploadThread = std::this_thread::get_id();
                ++CallbackCount;
                CallbackCondition.notify_all();
            }));
        ASSERT_TRUE(AsyncCommands.mValue->CopyBufferDeviceToHostAsync(
            *Buffer.mValue,
            [&](FArdaRHIBufferReadbackResult Result)
            {
                std::lock_guard<std::mutex> Lock(CallbackMutex);
                AsyncReadback = eastl::move(Result);
                ReadbackThread = std::this_thread::get_id();
                ++CallbackCount;
                CallbackCondition.notify_all();
            }));
        ASSERT_TRUE(AsyncCommands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(AsyncCommands.mValue));
        {
            std::unique_lock<std::mutex> Lock(CallbackMutex);
            ASSERT_TRUE(CallbackCondition.wait_for(
                Lock, std::chrono::seconds(10),
                [&] { return CallbackCount == 2; }))
                << Module.mName.c_str();
        }
        EXPECT_TRUE(UploadStatus) << UploadStatus.mMessage.c_str();
        EXPECT_TRUE(AsyncReadback) << AsyncReadback.mStatus.mMessage.c_str();
        EXPECT_EQ(AsyncReadback.mValue, Expected) << Module.mName.c_str();
        EXPECT_NE(UploadThread, CallingThread);
        EXPECT_NE(ReadbackThread, CallingThread);

        BlockingCommands.mValue = nullptr;
        AsyncCommands.mValue = nullptr;
        Buffer.mValue = nullptr;
        ASSERT_TRUE(Device->WaitForIdle());
        Device->RunGarbageCollection();
        const auto After = Device->GetResourceLifetimeStats();
        EXPECT_EQ(After.GetLiveResourceCount(EArdaRHIResourceType::Buffer),
            Baseline.GetLiveResourceCount(EArdaRHIResourceType::Buffer));
        EXPECT_EQ(After.mPendingSubmissions, Baseline.mPendingSubmissions);
        Device = nullptr;
        ShutdownBackend();
    }
    EXPECT_GT(TestedBackends, 0u);
}

#if defined(ARDA_TEST_NATIVE_VULKAN)
TEST(ArdaBackend, VulkanMergesStageLayoutsThatShareARegisterSpace)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    ShutdownBackend();
    FCollectingDiagnosticCallback Diagnostics;
    FExternalTestCleanup Cleanup;
    IArdaBackendModule* Module = FindBackendModule("native-vulkan");
    ASSERT_NE(Module, nullptr);
    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = Module->GetDescriptor().mName;
    Configuration.mBackend = EArdaBackendType::Vulkan;
    Configuration.mbEnableValidation = true;
    Configuration.mMessageCallback = &Diagnostics;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
    FArdaRHIDeviceRef Device = GetDevice();
    ASSERT_TRUE(Device);
    Device->TrimDescriptorCaches();
    const auto Baseline = Device->GetResourceLifetimeStats();

    {
        auto VertexShader = CreateArtifactShader(
            *Device, EArdaBackendType::Vulkan, "ArdaBindingSpaceVS",
            "BindingSpaceVS", EArdaRHIShaderStage::Vertex);
        auto PixelShader = CreateArtifactShader(
            *Device, EArdaBackendType::Vulkan, "ArdaBindingSpacePS",
            "BindingSpacePS", EArdaRHIShaderStage::Pixel);
        ASSERT_TRUE(VertexShader);
        ASSERT_TRUE(PixelShader);

        FArdaRHIBindingLayoutDesc VertexLayoutDesc;
        VertexLayoutDesc.mVisibility = EArdaRHIShaderStage::Vertex;
        VertexLayoutDesc.mRegisterSpace = 0;
        VertexLayoutDesc.mItems.push_back(
            { 0, 1, EArdaRHIBindingType::ConstantBuffer });
        FArdaRHIBindingLayoutDesc PixelLayoutDesc;
        PixelLayoutDesc.mVisibility = EArdaRHIShaderStage::Pixel;
        PixelLayoutDesc.mRegisterSpace = 0;
        PixelLayoutDesc.mItems.push_back(
            { 0, 1, EArdaRHIBindingType::TextureSRV });
        auto VertexLayout = Device->CreateBindingLayout(VertexLayoutDesc);
        auto PixelLayout = Device->CreateBindingLayout(PixelLayoutDesc);
        ASSERT_TRUE(VertexLayout);
        ASSERT_TRUE(PixelLayout);

        FArdaRHIBufferDesc ConstantsDesc;
        ConstantsDesc.mDebugName = "BindingSpaceConstants";
        ConstantsDesc.mByteSize = 256;
        ConstantsDesc.mUsage = EArdaRHIBufferUsage::Constant;
        ConstantsDesc.mCpuAccess = EArdaRHICpuAccess::Write;
        ConstantsDesc.mInitialState = EArdaRHIResourceState::ConstantBuffer;
        ConstantsDesc.mbKeepInitialState = true;
        auto Constants = Device->CreateBuffer(ConstantsDesc);
        ASSERT_TRUE(Constants);

        FArdaRHITextureDesc SourceDesc;
        SourceDesc.mDebugName = "BindingSpaceSource";
        SourceDesc.mWidth = 1;
        SourceDesc.mHeight = 1;
        SourceDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
        SourceDesc.mUsage = EArdaRHITextureUsage::ShaderResource;
        SourceDesc.mInitialState = EArdaRHIResourceState::ShaderResource;
        SourceDesc.mbKeepInitialState = true;
        auto Source = Device->CreateTexture(SourceDesc);
        ASSERT_TRUE(Source);

        FArdaRHITextureDesc TargetDesc = SourceDesc;
        TargetDesc.mDebugName = "BindingSpaceTarget";
        TargetDesc.mUsage = EArdaRHITextureUsage::RenderTarget;
        TargetDesc.mInitialState = EArdaRHIResourceState::RenderTarget;
        auto Target = Device->CreateTexture(TargetDesc);
        ASSERT_TRUE(Target);
        FArdaRHIFramebufferDesc FramebufferDesc;
        FramebufferDesc.mColorAttachments.push_back({ Target.mValue, {} });
        auto Framebuffer = Device->CreateFramebuffer(FramebufferDesc);
        ASSERT_TRUE(Framebuffer);

        FArdaRHIBindingSetDesc VertexSetDesc;
        VertexSetDesc.mLayout = VertexLayout.mValue;
        VertexSetDesc.mItems.push_back({
            0, 0, EArdaRHIBindingType::ConstantBuffer,
            TArdaRHIRef<IArdaRHIResource>(Constants.mValue.Get()), {} });
        FArdaRHIBindingSetDesc PixelSetDesc;
        PixelSetDesc.mLayout = PixelLayout.mValue;
        PixelSetDesc.mItems.push_back({
            0, 0, EArdaRHIBindingType::TextureSRV,
            TArdaRHIRef<IArdaRHIResource>(Source.mValue.Get()), {} });
        auto VertexSet = Device->CreateBindingSet(VertexSetDesc);
        auto PixelSet = Device->CreateBindingSet(PixelSetDesc);
        ASSERT_TRUE(VertexSet);
        ASSERT_TRUE(PixelSet);

        FArdaRHIGraphicsPipelineDesc PipelineDesc;
        PipelineDesc.mVertexShader = VertexShader.mValue;
        PipelineDesc.mPixelShader = PixelShader.mValue;
        PipelineDesc.mBindingLayouts.push_back(VertexLayout.mValue);
        PipelineDesc.mBindingLayouts.push_back(PixelLayout.mValue);
        PipelineDesc.mColorFormats.push_back(EArdaRHIFormat::RGBA8UNorm);
        PipelineDesc.mDepthStencilState.mbDepthTest = false;
        PipelineDesc.mDepthStencilState.mbDepthWrite = false;
        auto Pipeline = Device->CreateGraphicsPipeline(PipelineDesc);
        ASSERT_TRUE(Pipeline);

        auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        Commands.mValue->SetAutomaticBarriers(false);
        ASSERT_TRUE(Commands.mValue->BeginTrackingTextureState(
            *Source.mValue,
            {},
            EArdaRHIResourceState::ShaderResource));
        ASSERT_TRUE(Commands.mValue->SetTextureState(
            *Source.mValue, {}, EArdaRHIResourceState::ShaderResource));
        ASSERT_TRUE(Commands.mValue->BeginTrackingTextureState(
            *Target.mValue,
            {},
            EArdaRHIResourceState::RenderTarget));
        ASSERT_TRUE(Commands.mValue->SetTextureState(
            *Target.mValue, {}, EArdaRHIResourceState::RenderTarget));
        ASSERT_TRUE(Commands.mValue->ClearTexture(
            *Target.mValue,
            {},
            { 0.0f, 0.0f, 0.0f, 1.0f }));
        FArdaRHIGraphicsState State;
        State.mPipeline = Pipeline.mValue;
        State.mFramebuffer = Framebuffer.mValue;
        State.mBindings.push_back(VertexSet.mValue);
        State.mBindings.push_back(PixelSet.mValue);
        ASSERT_TRUE(Commands.mValue->SetGraphicsState(State));
        Commands.mValue->Draw({ 3 });
        ASSERT_TRUE(Commands.mValue->Close());

        const auto Recorded = Device->GetResourceLifetimeStats();
        EXPECT_GE(Recorded.mDescriptorSets, Baseline.mDescriptorSets + 3u);
        auto Submitted = Device->ExecuteCommandList(Commands.mValue);
        ASSERT_TRUE(Submitted);
        ASSERT_TRUE(Device->WaitForIdle());
    }

    Device->RunGarbageCollection();
    Device->TrimDescriptorCaches();
    const auto After = Device->GetResourceLifetimeStats();
    EXPECT_EQ(After.mDescriptorSets, Baseline.mDescriptorSets);
    EXPECT_EQ(After.mPendingSubmissions, Baseline.mPendingSubmissions);
    EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
}

TEST(ArdaBackend, VulkanPreservesPerMipLayoutsAcrossClearAndCompute)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    ShutdownBackend();
    FCollectingDiagnosticCallback Diagnostics;
    FExternalTestCleanup Cleanup;
    IArdaBackendModule* Module = FindBackendModule("native-vulkan");
    ASSERT_NE(Module, nullptr);
    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = Module->GetDescriptor().mName;
    Configuration.mBackend = EArdaBackendType::Vulkan;
    Configuration.mbEnableValidation = true;
    Configuration.mMessageCallback = &Diagnostics;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    ASSERT_TRUE(InitializeBackend()) << GetBackendError().c_str();
    FArdaRHIDeviceRef Device = GetDevice();
    ASSERT_TRUE(Device);

    auto Shader = CreateArtifactShader(
        *Device,
        EArdaBackendType::Vulkan,
        "ArdaVulkanLayoutTest",
        "VulkanLayoutTestCS",
        EArdaRHIShaderStage::Compute);
    ASSERT_TRUE(Shader);

    FArdaRHIBindingLayoutDesc LayoutDesc;
    LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
    LayoutDesc.mItems.push_back(
        { 0, 1, EArdaRHIBindingType::TextureUAV });
    auto Layout = Device->CreateBindingLayout(LayoutDesc);
    ASSERT_TRUE(Layout);

    FArdaRHITextureDesc TextureDesc;
    TextureDesc.mDebugName = "Vulkan per-mip layout test";
    TextureDesc.mWidth = 4;
    TextureDesc.mHeight = 4;
    TextureDesc.mMipLevels = 2;
    TextureDesc.mFormat = EArdaRHIFormat::RGBA32Float;
    TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource |
        EArdaRHITextureUsage::UnorderedAccess;
    TextureDesc.mInitialState = EArdaRHIResourceState::Common;
    auto Texture = Device->CreateTexture(TextureDesc);
    ASSERT_TRUE(Texture);

    FArdaRHIBindingSetDesc SetDesc;
    SetDesc.mLayout = Layout.mValue;
    FArdaRHIBindingItem Output;
    Output.mSlot = 0;
    Output.mType = EArdaRHIBindingType::TextureUAV;
    Output.mResource = TArdaRHIRef<IArdaRHIResource>(Texture.mValue.Get());
    Output.mView.mTextureRange = { 0, 1, 0, 1 };
    SetDesc.mItems.push_back(eastl::move(Output));
    auto BindingSet = Device->CreateBindingSet(SetDesc);
    ASSERT_TRUE(BindingSet);

    FArdaRHIComputePipelineDesc PipelineDesc;
    PipelineDesc.mComputeShader = Shader.mValue;
    PipelineDesc.mBindingLayouts.push_back(Layout.mValue);
    auto Pipeline = Device->CreateComputePipeline(PipelineDesc);
    ASSERT_TRUE(Pipeline);

    auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
    ASSERT_TRUE(Commands);
    ASSERT_TRUE(Commands.mValue->Open());
    Commands.mValue->SetAutomaticBarriers(false);
    const FArdaRHITextureSubresourceRange Mip0{ 0, 1, 0, 1 };
    const FArdaRHITextureSubresourceRange Mip1{ 1, 1, 0, 1 };
    ASSERT_TRUE(Commands.mValue->BeginTrackingTextureState(
        *Texture.mValue, Mip0, EArdaRHIResourceState::Common));
    ASSERT_TRUE(Commands.mValue->SetTextureState(
        *Texture.mValue, Mip0, EArdaRHIResourceState::UnorderedAccess));
    ASSERT_TRUE(Commands.mValue->ClearTexture(
        *Texture.mValue, Mip0, { 0.0f, 0.0f, 0.0f, 0.0f }));
    FArdaRHIComputeState State;
    State.mPipeline = Pipeline.mValue;
    State.mBindings.push_back(BindingSet.mValue);
    ASSERT_TRUE(Commands.mValue->SetComputeState(State));
    Commands.mValue->Dispatch(1, 1, 1);
    ASSERT_TRUE(Commands.mValue->SetTextureState(
        *Texture.mValue, Mip0, EArdaRHIResourceState::ShaderResource));
    ASSERT_TRUE(Commands.mValue->BeginTrackingTextureState(
        *Texture.mValue, Mip1, EArdaRHIResourceState::Common));
    ASSERT_TRUE(Commands.mValue->SetTextureState(
        *Texture.mValue, Mip1, EArdaRHIResourceState::ShaderResource));
    ASSERT_TRUE(Commands.mValue->Close());
    ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
    ASSERT_TRUE(Device->WaitForIdle());
    EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
}
#endif

#if defined(_WIN32) && defined(ARDA_TEST_NATIVE_D3D12)
TEST(ArdaBackend, InitializesD3D12Device)
{
    VerifyDeviceInitialization(arda::backend::EArdaBackendType::D3D12, false);
}
#endif

TEST(ArdaBackend, InitializesVulkanDevice)
{
    VerifyDeviceInitialization(arda::backend::EArdaBackendType::Vulkan, false);
}
