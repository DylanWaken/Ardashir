#include "ArdaBackend.h"

#include <gtest/gtest.h>

#include <fstream>
#include <vector>

#if defined(_WIN32)
#include <d3d12.h>
#include <wrl/client.h>
#endif

#ifndef ARDA_BACKEND_TEST_SHADER_DIR
#define ARDA_BACKEND_TEST_SHADER_DIR "."
#endif

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

#if defined(_WIN32)
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
