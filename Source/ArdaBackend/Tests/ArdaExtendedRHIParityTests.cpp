#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#if defined(ARDA_TEST_NATIVE_VULKAN)
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#endif
#endif

#ifndef ARDA_BACKEND_TEST_SHADER_DIR
#define ARDA_BACKEND_TEST_SHADER_DIR "."
#endif

namespace
{
    class FExtendedDiagnosticCallback final
        : public arda::backend::IArdaDiagnosticCallback
    {
    public:
        void Message(
            arda::backend::EArdaDiagnosticSeverity Severity,
            const char* Message) override
        {
            if (Severity == arda::backend::EArdaDiagnosticSeverity::Error ||
                Severity == arda::backend::EArdaDiagnosticSeverity::Fatal)
            {
                mErrorCount.fetch_add(1, std::memory_order_relaxed);
                if (Message)
                    std::fprintf(stderr, "Arda native validation: %s\n",
                        Message);
            }
        }

        [[nodiscard]] uint32_t GetErrorCount() const noexcept
        {
            return mErrorCount.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<uint32_t> mErrorCount{0};
    };

    class FExtendedBackendCleanup final
    {
    public:
        ~FExtendedBackendCleanup()
        {
            arda::backend::ShutdownBackend();
        }
    };

#if defined(_WIN32)
    class FWin32TestSurface final : public arda::backend::IArdaWindowSurface
    {
    public:
        FWin32TestSurface()
        {
            mWindow = CreateWindowExW(
                0, L"STATIC", L"Arda custom-present conformance",
                WS_OVERLAPPED, CW_USEDEFAULT, CW_USEDEFAULT, 64, 64,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        }

        ~FWin32TestSurface() override
        {
            if (mWindow) DestroyWindow(mWindow);
        }

        [[nodiscard]] bool IsValid() const noexcept { return mWindow != nullptr; }

        [[nodiscard]] arda::backend::FArdaNativeObject
            GetD3D12WindowHandle() const noexcept override
        {
            return arda::backend::FArdaNativeObject(mWindow);
        }

        [[nodiscard]] eastl::vector<const char*>
            GetVulkanInstanceExtensions() const override
        {
#if defined(ARDA_TEST_NATIVE_VULKAN)
            return {
                VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_WIN32_SURFACE_EXTENSION_NAME
            };
#else
            return {};
#endif
        }

        [[nodiscard]] arda::backend::FArdaNativeObject CreateVulkanSurface(
            arda::backend::FArdaNativeObject NativeInstance,
            eastl::string& OutError) override
        {
#if defined(ARDA_TEST_NATIVE_VULKAN)
            const HMODULE Loader = GetModuleHandleW(L"vulkan-1.dll");
            if (!Loader)
            {
                OutError = "The Vulkan loader is not loaded.";
                return {};
            }
            const auto GetInstanceProcAddr = reinterpret_cast<
                PFN_vkGetInstanceProcAddr>(
                    GetProcAddress(Loader, "vkGetInstanceProcAddr"));
            const VkInstance Instance = NativeInstance.As<VkInstance>();
            const auto CreateSurface = GetInstanceProcAddr
                ? reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
                    GetInstanceProcAddr(Instance, "vkCreateWin32SurfaceKHR"))
                : nullptr;
            if (!Instance || !CreateSurface)
            {
                OutError = "VK_KHR_win32_surface is unavailable.";
                return {};
            }
            VkWin32SurfaceCreateInfoKHR Info{};
            Info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            Info.hinstance = GetModuleHandleW(nullptr);
            Info.hwnd = mWindow;
            VkSurfaceKHR Surface = VK_NULL_HANDLE;
            const VkResult Result = CreateSurface(
                Instance, &Info, nullptr, &Surface);
            if (Result != VK_SUCCESS || Surface == VK_NULL_HANDLE)
            {
                OutError = "vkCreateWin32SurfaceKHR failed.";
                return {};
            }
            OutError.clear();
            return arda::backend::FArdaNativeObject(
                reinterpret_cast<uintptr_t>(Surface));
#else
            static_cast<void>(NativeInstance);
            OutError = "Vulkan test support was not compiled.";
            return {};
#endif
        }

    private:
        HWND mWindow = nullptr;
    };

    class FCustomPresentTracker final : public arda::backend::IArdaCustomPresent
    {
    public:
        void OnBackBufferResize(uint32_t Width, uint32_t Height) override
        {
            ++mResizeCount;
            mWidth = Width;
            mHeight = Height;
        }

        [[nodiscard]] bool NeedsNativePresent() const noexcept override
        {
            return false;
        }

        [[nodiscard]] bool Present(
            arda::backend::FArdaNativeObject BackBuffer,
            uint32_t Width,
            uint32_t Height) override
        {
            ++mPresentCount;
            mBackBuffer = BackBuffer;
            mWidth = Width;
            mHeight = Height;
            return static_cast<bool>(BackBuffer);
        }

        void PostPresent() override { ++mPostPresentCount; }

        uint32_t mResizeCount = 0;
        uint32_t mPresentCount = 0;
        uint32_t mPostPresentCount = 0;
        uint32_t mWidth = 0;
        uint32_t mHeight = 0;
        arda::backend::FArdaNativeObject mBackBuffer;
    };
#endif

    std::vector<uint8_t> LoadExtendedShaderArtifact(const char* FileName)
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
    CreateExtendedShader(
        arda::rhi::IArdaRHIDevice& Device,
        arda::backend::EArdaBackendType Backend,
        const char* Artifact,
        const char* EntryPoint,
        arda::rhi::EArdaRHIShaderStage Stage)
    {
        using namespace arda;
        const eastl::string FileName = eastl::string(Artifact) +
            backend::GetShaderArtifactExtension(Backend);
        const std::vector<uint8_t> Bytecode =
            LoadExtendedShaderArtifact(FileName.c_str());
        if (Bytecode.empty())
        {
            return {
                {},
                rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidState,
                    "The indirect conformance shader artifact is missing.")
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

    arda::rhi::TArdaRHIResult<arda::rhi::FArdaRHIShaderRef>
    CreateExtendedComputeShader(
        arda::rhi::IArdaRHIDevice& Device,
        arda::backend::EArdaBackendType Backend)
    {
        return CreateExtendedShader(
            Device,
            Backend,
            "ArdaShaderStructTest",
            "ShaderStructTestCS",
            arda::rhi::EArdaRHIShaderStage::Compute);
    }

    void ExpectTextureState(
        arda::rhi::IArdaRHICommandList& Commands,
        arda::rhi::IArdaRHITexture& Texture,
        const arda::rhi::FArdaRHITextureSubresourceRange& Range,
        arda::rhi::EArdaRHIResourceState Expected)
    {
        const auto Snapshot = Commands.QueryTextureState(Texture, Range);
        ASSERT_TRUE(Snapshot) << Snapshot.mStatus.mMessage.c_str();
        EXPECT_TRUE(Snapshot.mValue.IsConsistent());
        EXPECT_TRUE(Snapshot.mValue.mNative.mbNativeCompatible);
        EXPECT_EQ(Snapshot.mValue.mFacadeState, Expected);
        EXPECT_EQ(Snapshot.mValue.mNative.mState, Expected);
    }

    void ExpectBufferState(
        arda::rhi::IArdaRHICommandList& Commands,
        arda::rhi::IArdaRHIBuffer& Buffer,
        arda::rhi::EArdaRHIResourceState Expected)
    {
        const auto Snapshot = Commands.QueryBufferState(Buffer);
        ASSERT_TRUE(Snapshot) << Snapshot.mStatus.mMessage.c_str();
        EXPECT_TRUE(Snapshot.mValue.IsConsistent());
        EXPECT_TRUE(Snapshot.mValue.mNative.mbNativeCompatible);
        EXPECT_EQ(Snapshot.mValue.mFacadeState, Expected);
        EXPECT_EQ(Snapshot.mValue.mNative.mState, Expected);
    }

    void ExpectSamplerFeedbackState(
        arda::rhi::IArdaRHICommandList& Commands,
        arda::rhi::IArdaRHISamplerFeedbackTexture& Texture,
        arda::rhi::EArdaRHIResourceState Expected)
    {
        const auto Snapshot =
            Commands.QuerySamplerFeedbackTextureState(Texture);
        ASSERT_TRUE(Snapshot) << Snapshot.mStatus.mMessage.c_str();
        EXPECT_TRUE(Snapshot.mValue.IsConsistent());
        EXPECT_TRUE(Snapshot.mValue.mNative.mbNativeCompatible);
        EXPECT_EQ(Snapshot.mValue.mFacadeState, Expected);
        EXPECT_EQ(Snapshot.mValue.mNative.mState, Expected);
    }

    void VerifySamplerFeedbackStateParity()
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule("native-d3d12");
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = EArdaBackendType::D3D12;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        if (Device->GetCapabilities().mSamplerFeedbackTier ==
            EArdaRHISamplerFeedbackTier::None)
            GTEST_SKIP() << "The D3D12 adapter has no sampler-feedback tier.";

        FArdaRHITextureDesc PairedDesc;
        PairedDesc.mDebugName = "Sampler feedback paired texture";
        PairedDesc.mWidth = 512;
        PairedDesc.mHeight = 512;
        PairedDesc.mMipLevels = 10;
        PairedDesc.mFormat = EArdaRHIFormat::BC7UNorm;
        PairedDesc.mUsage = EArdaRHITextureUsage::ShaderResource;
        PairedDesc.mbTiled = true;
        PairedDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Paired = Device->CreateTexture(PairedDesc);
        ASSERT_TRUE(Paired) << Paired.mStatus.mMessage.c_str();
        auto Tiling = Device->GetTextureTiling(Paired.mValue);
        ASSERT_TRUE(Tiling) << Tiling.mStatus.mMessage.c_str();

        FArdaRHISamplerFeedbackTextureDesc FeedbackDesc;
        FeedbackDesc.mDebugName = "Native sampler feedback map";
        FeedbackDesc.mMipRegionX =
            Tiling.mValue.mTileShape.mWidthInTexels;
        FeedbackDesc.mMipRegionY =
            Tiling.mValue.mTileShape.mHeightInTexels;
        FeedbackDesc.mMipRegionZ = 1;
        FeedbackDesc.mInitialState =
            EArdaRHIResourceState::UnorderedAccess;
        auto Feedback = Device->CreateSamplerFeedbackTexture(
            Paired.mValue, FeedbackDesc);
        ASSERT_TRUE(Feedback) << Feedback.mStatus.mMessage.c_str();
        EXPECT_NE(Feedback.mValue->GetPhysicalIdentity(), nullptr);

        FArdaRHITextureDesc DecodedDesc;
        DecodedDesc.mDebugName = "Decoded sampler feedback";
        DecodedDesc.mWidth = (PairedDesc.mWidth +
            FeedbackDesc.mMipRegionX - 1u) /
            FeedbackDesc.mMipRegionX;
        DecodedDesc.mHeight = (PairedDesc.mHeight +
            FeedbackDesc.mMipRegionY - 1u) /
            FeedbackDesc.mMipRegionY;
        DecodedDesc.mFormat = EArdaRHIFormat::R8UInt;
        DecodedDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Decoded = Device->CreateTexture(DecodedDesc);
        ASSERT_TRUE(Decoded) << Decoded.mStatus.mMessage.c_str();
        FArdaRHIStagingTextureDesc ReadbackDesc;
        ReadbackDesc.mDebugName = "Decoded sampler-feedback readback";
        ReadbackDesc.mTexture = DecodedDesc;
        ReadbackDesc.mCpuAccess = EArdaRHICpuAccess::Read;
        auto Readback = Device->CreateStagingTexture(ReadbackDesc);
        ASSERT_TRUE(Readback) << Readback.mStatus.mMessage.c_str();

        auto Commands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ExpectSamplerFeedbackState(
            *Commands.mValue, *Feedback.mValue,
            EArdaRHIResourceState::UnorderedAccess);
        ASSERT_TRUE(Commands.mValue->ClearSamplerFeedbackTexture(
            *Feedback.mValue));
        ExpectSamplerFeedbackState(
            *Commands.mValue, *Feedback.mValue,
            EArdaRHIResourceState::UnorderedAccess);
        ASSERT_TRUE(Commands.mValue->DecodeSamplerFeedbackTexture(
            *Decoded.mValue, *Feedback.mValue,
            EArdaRHIFormat::R8UInt));
        ExpectSamplerFeedbackState(
            *Commands.mValue, *Feedback.mValue,
            EArdaRHIResourceState::ResolveSource);
        ExpectTextureState(
            *Commands.mValue, *Decoded.mValue, {},
            EArdaRHIResourceState::ResolveDest);
        FArdaRHITextureTransitionDesc ToReadback;
        ToReadback.mStateBefore = EArdaRHIResourceState::ResolveDest;
        ToReadback.mStateAfter = EArdaRHIResourceState::CopySource;
        ToReadback.mSourcePipelines = EArdaRHIPipeline::Copy;
        ToReadback.mDestinationPipelines = EArdaRHIPipeline::Copy;
        ASSERT_TRUE(Commands.mValue->TransitionTexture(
            *Decoded.mValue, ToReadback));
        ExpectTextureState(
            *Commands.mValue, *Decoded.mValue, {},
            EArdaRHIResourceState::CopySource);
        FArdaRHITextureSlice Slice;
        Slice.mWidth = DecodedDesc.mWidth;
        Slice.mHeight = DecodedDesc.mHeight;
        Slice.mDepth = 1;
        ASSERT_TRUE(Commands.mValue->CopyTextureToStaging(
            *Readback.mValue, Slice, *Decoded.mValue, Slice));
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        auto Mapping = Device->MapStagingTexture(
            Readback.mValue, Slice, EArdaRHICpuAccess::Read);
        ASSERT_TRUE(Mapping) << Mapping.mStatus.mMessage.c_str();
        for (uint32_t Y = 0; Y < DecodedDesc.mHeight; ++Y)
        {
            const auto* Row = static_cast<const uint8_t*>(
                Mapping.mValue.mData) + Y * Mapping.mValue.mRowPitch;
            for (uint32_t X = 0; X < DecodedDesc.mWidth; ++X)
                EXPECT_EQ(Row[X], 0xffu);
        }
        ASSERT_TRUE(Device->UnmapStagingTexture(Readback.mValue));
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyExtendedCommands(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);

        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();

        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        const FArdaRHICapabilities& Capabilities = Device->GetCapabilities();
        EXPECT_TRUE(Capabilities.mbStagingTextures);
        EXPECT_TRUE(Capabilities.mbTextureCopies);
        EXPECT_TRUE(Capabilities.mbTextureResolve);
        EXPECT_TRUE(Capabilities.mbExplicitTransitions);
        EXPECT_TRUE(Capabilities.mbSplitTransitions);
        EXPECT_TRUE(Capabilities.mbIndirectCommands);
        EXPECT_TRUE(Capabilities.mbAliasingBarriers);

        FArdaRHITextureDesc TextureDesc;
        TextureDesc.mDebugName = "Extended copy source";
        TextureDesc.mWidth = 4;
        TextureDesc.mHeight = 4;
        TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
        TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource |
            EArdaRHITextureUsage::UnorderedAccess;
        TextureDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Source = Device->CreateTexture(TextureDesc);
        TextureDesc.mDebugName = "Extended copy destination";
        auto Destination = Device->CreateTexture(TextureDesc);
        ASSERT_TRUE(Source);
        ASSERT_TRUE(Destination);

        FArdaRHIStagingTextureDesc UploadDesc;
        UploadDesc.mDebugName = "Extended upload staging";
        UploadDesc.mTexture = TextureDesc;
        UploadDesc.mCpuAccess = EArdaRHICpuAccess::Write;
        FArdaRHIStagingTextureDesc ReadbackDesc = UploadDesc;
        ReadbackDesc.mDebugName = "Extended readback staging";
        ReadbackDesc.mCpuAccess = EArdaRHICpuAccess::Read;
        auto Upload = Device->CreateStagingTexture(UploadDesc);
        auto Readback = Device->CreateStagingTexture(ReadbackDesc);
        ASSERT_TRUE(Upload);
        ASSERT_TRUE(Readback);

        FArdaRHITextureSlice WholeSlice;
        WholeSlice.mWidth = 4;
        WholeSlice.mHeight = 4;
        WholeSlice.mDepth = 1;
        auto UploadMapping = Device->MapStagingTexture(
            Upload.mValue, WholeSlice, EArdaRHICpuAccess::Write);
        ASSERT_TRUE(UploadMapping);
        ASSERT_NE(UploadMapping.mValue.mData, nullptr);
        ASSERT_GE(UploadMapping.mValue.mRowPitch, 16u);
        for (uint32_t Y = 0; Y < 4; ++Y)
        {
            auto* Row = static_cast<uint8_t*>(UploadMapping.mValue.mData) +
                Y * UploadMapping.mValue.mRowPitch;
            for (uint32_t X = 0; X < 4; ++X)
            {
                const uint8_t Pixel[4] = {
                    static_cast<uint8_t>(17 + X),
                    static_cast<uint8_t>(91 + Y),
                    203,
                    255};
                std::memcpy(Row + X * 4, Pixel, sizeof(Pixel));
            }
        }
        ASSERT_TRUE(Device->UnmapStagingTexture(Upload.mValue));

        FArdaRHITextureSlice TextureRegion;
        TextureRegion.mWidth = 2;
        TextureRegion.mHeight = 2;
        TextureRegion.mDepth = 1;
        FArdaRHITextureSlice StagingRegion = TextureRegion;
        StagingRegion.mX = 1;
        StagingRegion.mY = 1;

        FArdaRHIBufferDesc OutputDesc;
        OutputDesc.mDebugName = "Extended indirect output";
        OutputDesc.mByteSize = sizeof(uint32_t);
        OutputDesc.mStructureStride = sizeof(uint32_t);
        OutputDesc.mUsage = EArdaRHIBufferUsage::Structured |
            EArdaRHIBufferUsage::UnorderedAccess;
        OutputDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Output = Device->CreateBuffer(OutputDesc);
        ASSERT_TRUE(Output);

        FArdaRHIBufferDesc ArgumentsDesc;
        ArgumentsDesc.mDebugName = "Extended indirect arguments";
        ArgumentsDesc.mByteSize = 3 * sizeof(uint32_t);
        ArgumentsDesc.mUsage = EArdaRHIBufferUsage::Indirect;
        ArgumentsDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Arguments = Device->CreateBuffer(ArgumentsDesc);
        ASSERT_TRUE(Arguments);

        auto Shader = CreateExtendedComputeShader(*Device, Backend);
        ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
        LayoutDesc.mItems.push_back(
            { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(Layout);
        FArdaRHIBindingSetDesc SetDesc;
        SetDesc.mLayout = Layout.mValue;
        FArdaRHIBindingItem OutputBinding;
        OutputBinding.mSlot = 0;
        OutputBinding.mType = EArdaRHIBindingType::StructuredBufferUAV;
        OutputBinding.mResource =
            TArdaRHIRef<IArdaRHIResource>(Output.mValue.Get());
        SetDesc.mItems.push_back(OutputBinding);
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
        const FArdaRHITextureSubresourceRange WholeRange{};
        ExpectTextureState(
            *Commands.mValue, *Source.mValue, WholeRange,
            EArdaRHIResourceState::Common);
        ExpectTextureState(
            *Commands.mValue, *Destination.mValue, WholeRange,
            EArdaRHIResourceState::Common);

        ASSERT_TRUE(Commands.mValue->CopyTextureFromStaging(
            *Source.mValue, TextureRegion,
            *Upload.mValue, StagingRegion));
        ExpectTextureState(
            *Commands.mValue, *Source.mValue, WholeRange,
            EArdaRHIResourceState::Common);
        ASSERT_TRUE(Commands.mValue->CopyTexture(
            *Destination.mValue, TextureRegion,
            *Source.mValue, TextureRegion));
        ExpectTextureState(
            *Commands.mValue, *Source.mValue, WholeRange,
            EArdaRHIResourceState::Common);
        ExpectTextureState(
            *Commands.mValue, *Destination.mValue, WholeRange,
            EArdaRHIResourceState::Common);
        ASSERT_TRUE(Commands.mValue->CopyTextureToStaging(
            *Readback.mValue, StagingRegion,
            *Destination.mValue, TextureRegion));
        ExpectTextureState(
            *Commands.mValue, *Destination.mValue, WholeRange,
            EArdaRHIResourceState::Common);

        FArdaRHITextureTransitionDesc TextureTransition;
        TextureTransition.mStateBefore = EArdaRHIResourceState::Common;
        TextureTransition.mStateAfter = EArdaRHIResourceState::CopyDest;
        TextureTransition.mSourcePipelines = EArdaRHIPipeline::Copy;
        TextureTransition.mDestinationPipelines = EArdaRHIPipeline::Graphics;
        ASSERT_TRUE(Commands.mValue->TransitionTexture(
            *Destination.mValue, TextureTransition));
        ExpectTextureState(
            *Commands.mValue, *Destination.mValue, WholeRange,
            EArdaRHIResourceState::CopyDest);

        FArdaRHITextureTransitionDesc BadBefore = TextureTransition;
        BadBefore.mStateBefore = EArdaRHIResourceState::Common;
        BadBefore.mStateAfter = EArdaRHIResourceState::UnorderedAccess;
        const FArdaRHIStatus Rejected = Commands.mValue->TransitionTexture(
            *Destination.mValue, BadBefore);
        EXPECT_FALSE(Rejected);
        EXPECT_EQ(Rejected.mCode, EArdaRHIResult::InvalidState);
        ExpectTextureState(
            *Commands.mValue, *Destination.mValue, WholeRange,
            EArdaRHIResourceState::CopyDest);

        FArdaRHITextureTransitionDesc SplitTransition;
        SplitTransition.mStateBefore = EArdaRHIResourceState::CopyDest;
        SplitTransition.mStateAfter = EArdaRHIResourceState::ShaderResource;
        SplitTransition.mSourcePipelines = EArdaRHIPipeline::Copy;
        SplitTransition.mDestinationPipelines = EArdaRHIPipeline::Graphics;
        SplitTransition.mFlags = EArdaRHITransitionFlags::BeginOnly;
        ASSERT_TRUE(Commands.mValue->TransitionTexture(
            *Destination.mValue, SplitTransition));
        ExpectTextureState(
            *Commands.mValue, *Destination.mValue, WholeRange,
            EArdaRHIResourceState::CopyDest);
        SplitTransition.mFlags = EArdaRHITransitionFlags::EndOnly;
        ASSERT_TRUE(Commands.mValue->TransitionTexture(
            *Destination.mValue, SplitTransition));
        ExpectTextureState(
            *Commands.mValue, *Destination.mValue, WholeRange,
            EArdaRHIResourceState::ShaderResource);

        const uint32_t DispatchArguments[3] = { 1, 1, 1 };
        ASSERT_TRUE(Commands.mValue->WriteBuffer(
            *Arguments.mValue,
            DispatchArguments,
            sizeof(DispatchArguments)));
        ExpectBufferState(
            *Commands.mValue,
            *Arguments.mValue,
            EArdaRHIResourceState::Common);
        FArdaRHIBufferTransitionDesc ArgumentTransition;
        ArgumentTransition.mStateBefore = EArdaRHIResourceState::Common;
        ArgumentTransition.mStateAfter =
            EArdaRHIResourceState::IndirectArgument;
        ArgumentTransition.mSourcePipelines = EArdaRHIPipeline::Copy;
        ArgumentTransition.mDestinationPipelines = EArdaRHIPipeline::AsyncCompute;
        ASSERT_TRUE(Commands.mValue->TransitionBuffer(
            *Arguments.mValue, ArgumentTransition));
        ExpectBufferState(
            *Commands.mValue,
            *Arguments.mValue,
            EArdaRHIResourceState::IndirectArgument);
        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *Output.mValue, EArdaRHIResourceState::UnorderedAccess));
        ExpectBufferState(
            *Commands.mValue,
            *Output.mValue,
            EArdaRHIResourceState::UnorderedAccess);

        FArdaRHIComputeState ComputeState;
        ComputeState.mPipeline = Pipeline.mValue;
        ComputeState.mBindings.push_back(BindingSet.mValue);
        ASSERT_TRUE(Commands.mValue->SetComputeState(ComputeState));
        ASSERT_TRUE(Commands.mValue->DispatchIndirect(*Arguments.mValue));
        ExpectBufferState(
            *Commands.mValue,
            *Arguments.mValue,
            EArdaRHIResourceState::IndirectArgument);
        const FArdaRHIStatus OutOfBounds =
            Commands.mValue->DispatchIndirect(*Arguments.mValue, 4);
        EXPECT_FALSE(OutOfBounds);
        EXPECT_EQ(OutOfBounds.mCode, EArdaRHIResult::InvalidArgument);

        eastl::vector<uint8_t> OutputReadback;
        ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(
            *Output.mValue,
            OutputReadback,
            0,
            sizeof(uint32_t)));
        ExpectBufferState(
            *Commands.mValue,
            *Output.mValue,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());

        auto ReadbackMapping = Device->MapStagingTexture(
            Readback.mValue, WholeSlice, EArdaRHICpuAccess::Read);
        ASSERT_TRUE(ReadbackMapping)
            << ReadbackMapping.mStatus.mMessage.c_str();
        ASSERT_NE(ReadbackMapping.mValue.mData, nullptr);
        ASSERT_GE(ReadbackMapping.mValue.mRowPitch, 16u);
        for (uint32_t Y = 0; Y < 2; ++Y)
        {
            const auto* Row =
                static_cast<const uint8_t*>(ReadbackMapping.mValue.mData) +
                (Y + StagingRegion.mY) * ReadbackMapping.mValue.mRowPitch;
            for (uint32_t X = 0; X < 2; ++X)
            {
                const uint8_t ExpectedPixel[4] = {
                    static_cast<uint8_t>(17 + X + StagingRegion.mX),
                    static_cast<uint8_t>(91 + Y + StagingRegion.mY),
                    203,
                    255};
                EXPECT_EQ(0, std::memcmp(
                    Row + (X + StagingRegion.mX) * 4,
                    ExpectedPixel,
                    sizeof(ExpectedPixel)));
            }
        }
        ASSERT_TRUE(Device->UnmapStagingTexture(Readback.mValue));
        ASSERT_EQ(OutputReadback.size(), sizeof(uint32_t));
        uint32_t OutputValue = 0;
        std::memcpy(
            &OutputValue, OutputReadback.data(), sizeof(OutputValue));
        EXPECT_EQ(OutputValue, 0xA2DAu);

        auto Persisted = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Persisted);
        ASSERT_TRUE(Persisted.mValue->Open());
        ExpectTextureState(
            *Persisted.mValue,
            *Destination.mValue,
            WholeRange,
            EArdaRHIResourceState::ShaderResource);
        ExpectBufferState(
            *Persisted.mValue,
            *Arguments.mValue,
            EArdaRHIResourceState::IndirectArgument);
        ExpectBufferState(
            *Persisted.mValue,
            *Output.mValue,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(Persisted.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Persisted.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyResolveAndPlaneTracking(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);

        FArdaRHITextureDesc SourceDesc;
        SourceDesc.mDebugName = "Extended MSAA resolve source";
        SourceDesc.mWidth = 4;
        SourceDesc.mHeight = 4;
        SourceDesc.mSampleCount = 2;
        SourceDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
        SourceDesc.mUsage = EArdaRHITextureUsage::RenderTarget;
        SourceDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Source = Device->CreateTexture(SourceDesc);
        ASSERT_TRUE(Source) << Source.mStatus.mMessage.c_str();
        FArdaRHITextureDesc DestinationDesc = SourceDesc;
        DestinationDesc.mDebugName = "Extended resolve destination";
        DestinationDesc.mSampleCount = 1;
        DestinationDesc.mUsage = EArdaRHITextureUsage::ShaderResource;
        auto Destination = Device->CreateTexture(DestinationDesc);
        ASSERT_TRUE(Destination);

        FArdaRHIStagingTextureDesc ReadbackDesc;
        ReadbackDesc.mDebugName = "Extended resolve readback";
        ReadbackDesc.mTexture = DestinationDesc;
        ReadbackDesc.mCpuAccess = EArdaRHICpuAccess::Read;
        auto Readback = Device->CreateStagingTexture(ReadbackDesc);
        ASSERT_TRUE(Readback);

        FArdaRHITextureDesc DepthDesc;
        DepthDesc.mDebugName = "Extended depth stencil plane state";
        DepthDesc.mWidth = 4;
        DepthDesc.mHeight = 4;
        DepthDesc.mFormat = EArdaRHIFormat::D24S8;
        DepthDesc.mUsage = EArdaRHITextureUsage::DepthStencil;
        DepthDesc.mInitialState = EArdaRHIResourceState::Common;
        auto DepthStencil = Device->CreateTexture(DepthDesc);
        ASSERT_TRUE(DepthStencil);

        auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        const FArdaRHITextureSubresourceRange WholeRange{};
        ASSERT_TRUE(Commands.mValue->SetTextureState(
            *Source.mValue,
            WholeRange,
            EArdaRHIResourceState::RenderTarget));
        ExpectTextureState(
            *Commands.mValue,
            *Source.mValue,
            WholeRange,
            EArdaRHIResourceState::RenderTarget);
        ASSERT_TRUE(Commands.mValue->ClearTexture(
            *Source.mValue,
            WholeRange,
            { 0.25f, 0.5f, 0.75f, 1.0f }));
        ExpectTextureState(
            *Commands.mValue,
            *Source.mValue,
            WholeRange,
            EArdaRHIResourceState::RenderTarget);

        FArdaRHITextureSlice Slice;
        Slice.mWidth = 4;
        Slice.mHeight = 4;
        Slice.mDepth = 1;
        ASSERT_TRUE(Commands.mValue->ResolveTexture(
            *Destination.mValue, Slice, *Source.mValue, Slice));
        ExpectTextureState(
            *Commands.mValue,
            *Source.mValue,
            WholeRange,
            EArdaRHIResourceState::RenderTarget);
        ExpectTextureState(
            *Commands.mValue,
            *Destination.mValue,
            WholeRange,
            EArdaRHIResourceState::Common);
        ASSERT_TRUE(Commands.mValue->CopyTextureToStaging(
            *Readback.mValue, Slice, *Destination.mValue, Slice));
        ExpectTextureState(
            *Commands.mValue,
            *Destination.mValue,
            WholeRange,
            EArdaRHIResourceState::Common);

        const FArdaRHITextureSubresourceRange DepthPlane{
            0, 1, 0, 1, 0, 1 };
        const FArdaRHITextureSubresourceRange StencilPlane{
            0, 1, 0, 1, 1, 1 };
        ASSERT_TRUE(Commands.mValue->SetTextureState(
            *DepthStencil.mValue,
            DepthPlane,
            EArdaRHIResourceState::DepthWrite));
        ExpectTextureState(
            *Commands.mValue,
            *DepthStencil.mValue,
            DepthPlane,
            EArdaRHIResourceState::DepthWrite);
        ExpectTextureState(
            *Commands.mValue,
            *DepthStencil.mValue,
            StencilPlane,
            EArdaRHIResourceState::Common);
        const auto MixedPlanes = Commands.mValue->QueryTextureState(
            *DepthStencil.mValue, WholeRange);
        EXPECT_FALSE(MixedPlanes);
        EXPECT_EQ(MixedPlanes.mStatus.mCode, EArdaRHIResult::InvalidState);

        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());

        auto Mapping = Device->MapStagingTexture(
            Readback.mValue, Slice, EArdaRHICpuAccess::Read);
        ASSERT_TRUE(Mapping) << Mapping.mStatus.mMessage.c_str();
        ASSERT_GE(Mapping.mValue.mRowPitch, 16u);
        for (uint32_t Y = 0; Y < 4; ++Y)
        {
            const auto* Row = static_cast<const uint8_t*>(
                Mapping.mValue.mData) + Y * Mapping.mValue.mRowPitch;
            for (uint32_t X = 0; X < 4; ++X)
            {
                EXPECT_NEAR(Row[X * 4 + 0], 64, 1);
                EXPECT_NEAR(Row[X * 4 + 1], 128, 1);
                EXPECT_NEAR(Row[X * 4 + 2], 191, 1);
                EXPECT_EQ(Row[X * 4 + 3], 255);
            }
        }
        ASSERT_TRUE(Device->UnmapStagingTexture(Readback.mValue));
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyExplicitHeapAliasing(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        ASSERT_TRUE(Device->GetCapabilities().mbVirtualResources);
        ASSERT_TRUE(Device->GetCapabilities().mbHeaps);

        FArdaRHIBufferDesc BufferDesc;
        BufferDesc.mDebugName = "Explicit heap alias A";
        BufferDesc.mByteSize = 256;
        BufferDesc.mStructureStride = sizeof(uint32_t);
        BufferDesc.mUsage = EArdaRHIBufferUsage::Structured |
            EArdaRHIBufferUsage::ShaderResource |
            EArdaRHIBufferUsage::UnorderedAccess;
        BufferDesc.mInitialState = EArdaRHIResourceState::Common;
        BufferDesc.mbVirtual = true;
        auto First = Device->CreateBuffer(BufferDesc);
        BufferDesc.mDebugName = "Explicit heap alias B";
        auto Second = Device->CreateBuffer(BufferDesc);
        ASSERT_TRUE(First);
        ASSERT_TRUE(Second);
        auto FirstRequirements =
            Device->GetBufferMemoryRequirements(First.mValue);
        auto SecondRequirements =
            Device->GetBufferMemoryRequirements(Second.mValue);
        ASSERT_TRUE(FirstRequirements);
        ASSERT_TRUE(SecondRequirements);
        EXPECT_GT(FirstRequirements.mValue.mSize, 0u);
        EXPECT_GT(FirstRequirements.mValue.mAlignment, 0u);
        EXPECT_EQ(
            FirstRequirements.mValue.mSize,
            SecondRequirements.mValue.mSize);
        EXPECT_EQ(
            FirstRequirements.mValue.mAlignment,
            SecondRequirements.mValue.mAlignment);

        FArdaRHIHeapDesc HeapDesc;
        HeapDesc.mDebugName = "Explicit overlapping buffer heap";
        HeapDesc.mCapacity = eastl::max(
            FirstRequirements.mValue.mSize,
            SecondRequirements.mValue.mSize);
        HeapDesc.mMemoryTypeBits =
            FirstRequirements.mValue.mMemoryTypeBits &
            SecondRequirements.mValue.mMemoryTypeBits;
        ASSERT_NE(HeapDesc.mMemoryTypeBits, 0u);
        auto Heap = Device->CreateHeap(HeapDesc);
        ASSERT_TRUE(Heap) << Heap.mStatus.mMessage.c_str();
        ASSERT_TRUE(Device->BindBufferMemory(First.mValue, Heap.mValue, 0));
        ASSERT_TRUE(Device->BindBufferMemory(Second.mValue, Heap.mValue, 0));
        const FArdaRHIStatus Rebind =
            Device->BindBufferMemory(First.mValue, Heap.mValue, 0);
        EXPECT_FALSE(Rebind);
        EXPECT_EQ(Rebind.mCode, EArdaRHIResult::InvalidState);

        auto Commands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ExpectBufferState(
            *Commands.mValue,
            *First.mValue,
            EArdaRHIResourceState::Common);
        ExpectBufferState(
            *Commands.mValue,
            *Second.mValue,
            EArdaRHIResourceState::Common);
        const uint32_t FirstPattern = 0x11111111u;
        const uint32_t SecondPattern = 0x6a7b8c9du;
        ASSERT_TRUE(Commands.mValue->WriteBuffer(
            *First.mValue, &FirstPattern, sizeof(FirstPattern)));
        ExpectBufferState(
            *Commands.mValue,
            *First.mValue,
            EArdaRHIResourceState::Common);
        ASSERT_TRUE(Commands.mValue->AliasingBarrier(
            First.mValue.Get(), Second.mValue.Get()));
        ExpectBufferState(
            *Commands.mValue,
            *Second.mValue,
            EArdaRHIResourceState::Common);
        ASSERT_TRUE(Commands.mValue->WriteBuffer(
            *Second.mValue, &SecondPattern, sizeof(SecondPattern)));
        ExpectBufferState(
            *Commands.mValue,
            *Second.mValue,
            EArdaRHIResourceState::Common);
        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(
            *Second.mValue, Readback, 0, sizeof(SecondPattern)));
        ExpectBufferState(
            *Commands.mValue,
            *Second.mValue,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        ASSERT_EQ(Readback.size(), sizeof(SecondPattern));
        uint32_t ActualPattern = 0;
        std::memcpy(
            &ActualPattern, Readback.data(), sizeof(ActualPattern));
        EXPECT_EQ(ActualPattern, SecondPattern);
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyBindlessDescriptorTable(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName,
        bool bDirectHeapIndexing = false)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        ASSERT_TRUE(Device->GetCapabilities().mDescriptors.mbBindless);
        if (bDirectHeapIndexing &&
            !Device->GetCapabilities().mDescriptors.
                mbDirectResourceHeapIndexing)
            GTEST_SKIP() << "Direct native resource-heap indexing is unavailable.";

        FArdaRHIBufferDesc OutputDesc;
        OutputDesc.mDebugName = "Bindless descriptor table output";
        OutputDesc.mByteSize = sizeof(uint32_t);
        OutputDesc.mStructureStride = sizeof(uint32_t);
        OutputDesc.mUsage = EArdaRHIBufferUsage::Structured |
            EArdaRHIBufferUsage::UnorderedAccess;
        OutputDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Output = Device->CreateBuffer(OutputDesc);
        ASSERT_TRUE(Output);

        FArdaRHIBindlessLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
        LayoutDesc.mMaxCapacity = 4;
        LayoutDesc.mLayoutType =
            EArdaRHIBindlessLayoutType::MutableSrvUavCbv;
        LayoutDesc.mbDirectHeapIndexing = bDirectHeapIndexing;
        LayoutDesc.mDebugName = "Bounded bindless UAV table";
        LayoutDesc.mbAllowUnsafeDescriptorTableLifetime = true;
        LayoutDesc.mRegisterSpaces.push_back(
            { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
        auto Layout = Device->CreateBindlessLayout(LayoutDesc);
        ASSERT_TRUE(Layout) << Layout.mStatus.mMessage.c_str();
        auto Table = Device->CreateDescriptorTable(Layout.mValue);
        ASSERT_TRUE(Table) << Table.mStatus.mMessage.c_str();
        EXPECT_EQ(Table.mValue->GetCapacity(), 4u);
        EXPECT_EQ(Table.mValue->GetFirstDescriptorIndexInHeap(), 0u);

        for (uint32_t Element = 0; Element < 4; ++Element)
        {
            FArdaRHIBindingItem Item;
            Item.mSlot = 0;
            Item.mArrayElement = Element;
            Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
            Item.mResource =
                TArdaRHIRef<IArdaRHIResource>(Output.mValue.Get());
            ASSERT_TRUE(Device->WriteDescriptorTable(Table.mValue, Item));
        }

        auto Shader = CreateExtendedComputeShader(*Device, Backend);
        ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
        FArdaRHIComputePipelineDesc PipelineDesc;
        PipelineDesc.mComputeShader = Shader.mValue;
        PipelineDesc.mBindingLayouts.push_back(Layout.mValue);
        auto Pipeline = Device->CreateComputePipeline(PipelineDesc);
        ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();
        auto Commands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *Output.mValue, EArdaRHIResourceState::UnorderedAccess));
        ExpectBufferState(
            *Commands.mValue,
            *Output.mValue,
            EArdaRHIResourceState::UnorderedAccess);
        FArdaRHIComputeState State;
        State.mPipeline = Pipeline.mValue;
        State.mBindings.push_back(
            FArdaRHIBindingSetRef(Table.mValue.Get()));
        ASSERT_TRUE(Commands.mValue->SetComputeState(State));
        Commands.mValue->Dispatch(1, 1, 1);
        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(
            *Output.mValue, Readback, 0, sizeof(uint32_t)));
        ExpectBufferState(
            *Commands.mValue,
            *Output.mValue,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        ASSERT_EQ(Readback.size(), sizeof(uint32_t));
        uint32_t Value = 0;
        std::memcpy(&Value, Readback.data(), sizeof(Value));
        EXPECT_EQ(Value, 0xA2DAu);

        ASSERT_TRUE(Device->ResizeDescriptorTable(Table.mValue, 2, true));
        EXPECT_EQ(Table.mValue->GetCapacity(), 2u);
        FArdaRHIBindingItem InvalidItem;
        InvalidItem.mSlot = 0;
        InvalidItem.mArrayElement = 2;
        InvalidItem.mType = EArdaRHIBindingType::StructuredBufferUAV;
        InvalidItem.mResource =
            TArdaRHIRef<IArdaRHIResource>(Output.mValue.Get());
        const FArdaRHIStatus InvalidWrite =
            Device->WriteDescriptorTable(Table.mValue, InvalidItem);
        EXPECT_FALSE(InvalidWrite);
        EXPECT_EQ(InvalidWrite.mCode, EArdaRHIResult::InvalidArgument);
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyDirectResourceAndSamplerHeapIndexing(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = BackendName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        auto Device = GetDevice();
        ASSERT_TRUE(Device);
        const auto& DescriptorCaps =
            Device->GetCapabilities().mDescriptors;
        if (!DescriptorCaps.mbDirectResourceHeapIndexing ||
            !DescriptorCaps.mbDirectSamplerHeapIndexing)
            GTEST_SKIP() << "Direct resource and sampler heaps are unavailable.";

        FArdaRHITextureDesc TextureDesc;
        TextureDesc.mDebugName = "Direct-heap sampled texture";
        TextureDesc.mWidth = 1;
        TextureDesc.mHeight = 1;
        TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
        TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource |
            EArdaRHITextureUsage::RenderTarget;
        TextureDesc.mInitialState = EArdaRHIResourceState::RenderTarget;
        auto Texture = Device->CreateTexture(TextureDesc);
        ASSERT_TRUE(Texture) << Texture.mStatus.mMessage.c_str();

        FArdaRHIBufferDesc OutputDesc;
        OutputDesc.mDebugName = "Direct-heap sampler output";
        OutputDesc.mByteSize = sizeof(uint32_t);
        OutputDesc.mStructureStride = sizeof(uint32_t);
        OutputDesc.mUsage = EArdaRHIBufferUsage::Structured |
            EArdaRHIBufferUsage::UnorderedAccess;
        auto Output = Device->CreateBuffer(OutputDesc);
        ASSERT_TRUE(Output) << Output.mStatus.mMessage.c_str();
        FArdaRHISamplerDesc SamplerDesc;
        SamplerDesc.mDebugName = "Direct-heap point sampler";
        SamplerDesc.mbMinFilter = false;
        SamplerDesc.mbMagFilter = false;
        SamplerDesc.mbMipFilter = false;
        auto Sampler = Device->CreateSampler(SamplerDesc);
        ASSERT_TRUE(Sampler) << Sampler.mStatus.mMessage.c_str();

        FArdaRHIBindlessLayoutDesc ResourceLayoutDesc;
        ResourceLayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
        ResourceLayoutDesc.mRegisterSpace = 0;
        ResourceLayoutDesc.mMaxCapacity = 1;
        ResourceLayoutDesc.mbDirectHeapIndexing = true;
        ResourceLayoutDesc.mLayoutType =
            EArdaRHIBindlessLayoutType::MutableSrvUavCbv;
        ResourceLayoutDesc.mRegisterSpaces.push_back(
            {0, 1, EArdaRHIBindingType::TextureSRV});
        ResourceLayoutDesc.mRegisterSpaces.push_back(
            {0, 1, EArdaRHIBindingType::StructuredBufferUAV});
        ResourceLayoutDesc.mDebugName = "Direct resource heap layout";
        auto ResourceLayout = Device->CreateBindlessLayout(
            ResourceLayoutDesc);
        ASSERT_TRUE(ResourceLayout)
            << ResourceLayout.mStatus.mMessage.c_str();
        auto ResourceTable = Device->CreateDescriptorTable(
            ResourceLayout.mValue);
        ASSERT_TRUE(ResourceTable)
            << ResourceTable.mStatus.mMessage.c_str();
        FArdaRHIBindingItem TextureBinding;
        TextureBinding.mType = EArdaRHIBindingType::TextureSRV;
        TextureBinding.mResource = FArdaRHIResourceRef(Texture.mValue.Get());
        ASSERT_TRUE(Device->WriteDescriptorTable(
            ResourceTable.mValue, TextureBinding));
        FArdaRHIBindingItem OutputBinding;
        OutputBinding.mType = EArdaRHIBindingType::StructuredBufferUAV;
        OutputBinding.mResource = FArdaRHIResourceRef(Output.mValue.Get());
        ASSERT_TRUE(Device->WriteDescriptorTable(
            ResourceTable.mValue, OutputBinding));

        FArdaRHIBindlessLayoutDesc SamplerLayoutDesc;
        SamplerLayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
        SamplerLayoutDesc.mRegisterSpace = 1;
        SamplerLayoutDesc.mMaxCapacity = 1;
        SamplerLayoutDesc.mbDirectHeapIndexing = true;
        SamplerLayoutDesc.mLayoutType =
            EArdaRHIBindlessLayoutType::MutableSampler;
        SamplerLayoutDesc.mRegisterSpaces.push_back(
            {0, 1, EArdaRHIBindingType::Sampler});
        SamplerLayoutDesc.mDebugName = "Direct sampler heap layout";
        auto SamplerLayout = Device->CreateBindlessLayout(
            SamplerLayoutDesc);
        ASSERT_TRUE(SamplerLayout)
            << SamplerLayout.mStatus.mMessage.c_str();
        auto SamplerTable = Device->CreateDescriptorTable(
            SamplerLayout.mValue);
        ASSERT_TRUE(SamplerTable)
            << SamplerTable.mStatus.mMessage.c_str();
        FArdaRHIBindingItem SamplerBinding;
        SamplerBinding.mType = EArdaRHIBindingType::Sampler;
        SamplerBinding.mResource = FArdaRHIResourceRef(Sampler.mValue.Get());
        ASSERT_TRUE(Device->WriteDescriptorTable(
            SamplerTable.mValue, SamplerBinding));

        auto Shader = CreateExtendedShader(
            *Device, Backend, "ArdaDirectHeapSamplerTest",
            "DirectHeapSamplerTestCS", EArdaRHIShaderStage::Compute);
        ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
        FArdaRHIComputePipelineDesc PipelineDesc;
        PipelineDesc.mComputeShader = Shader.mValue;
        PipelineDesc.mBindingLayouts = {
            ResourceLayout.mValue, SamplerLayout.mValue};
        auto Pipeline = Device->CreateComputePipeline(PipelineDesc);
        ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();

        auto Commands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ASSERT_TRUE(Commands.mValue->ClearTexture(
            *Texture.mValue, {}, {0.25f, 0.5f, 0.75f, 1.f}));
        ASSERT_TRUE(Commands.mValue->SetTextureState(
            *Texture.mValue, {}, EArdaRHIResourceState::ShaderResource));
        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *Output.mValue, EArdaRHIResourceState::UnorderedAccess));
        FArdaRHIComputeState State;
        State.mPipeline = Pipeline.mValue;
        State.mBindings = {
            FArdaRHIBindingSetRef(ResourceTable.mValue.Get()),
            FArdaRHIBindingSetRef(SamplerTable.mValue.Get())};
        ASSERT_TRUE(Commands.mValue->SetComputeState(State));
        Commands.mValue->Dispatch(1, 1, 1);
        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(
            *Output.mValue, Readback, 0, sizeof(uint32_t)));
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        uint32_t Value = 0;
        ASSERT_EQ(Readback.size(), sizeof(Value));
        std::memcpy(&Value, Readback.data(), sizeof(Value));
        EXPECT_EQ(Value, 0x5A4Du);
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyShaderBundleExecution(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;
        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = BackendName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        auto Device = GetDevice();
        ASSERT_TRUE(Device);
        ASSERT_TRUE(Device->QueryShaderBundleSupport());

        auto Shader = CreateExtendedComputeShader(*Device, Backend);
        ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
        LayoutDesc.mItems.push_back(
            {0, 1, EArdaRHIBindingType::StructuredBufferUAV});
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(Layout) << Layout.mStatus.mMessage.c_str();
        FArdaRHIComputePipelineDesc PipelineDesc;
        PipelineDesc.mComputeShader = Shader.mValue;
        PipelineDesc.mBindingLayouts.push_back(Layout.mValue);
        auto Pipeline = Device->CreateComputePipeline(PipelineDesc);
        ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();

        const auto CreateOutput = [&]()
        {
            FArdaRHIBufferDesc Desc;
            Desc.mByteSize = sizeof(uint32_t);
            Desc.mStructureStride = sizeof(uint32_t);
            Desc.mUsage = EArdaRHIBufferUsage::Structured |
                EArdaRHIBufferUsage::UnorderedAccess;
            return Device->CreateBuffer(Desc);
        };
        auto OutputA = CreateOutput();
        auto OutputB = CreateOutput();
        ASSERT_TRUE(OutputA);
        ASSERT_TRUE(OutputB);
        const auto CreateSet = [&](const FArdaRHIBufferRef& Output)
        {
            FArdaRHIBindingSetDesc Desc;
            Desc.mLayout = Layout.mValue;
            FArdaRHIBindingItem Item;
            Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
            Item.mResource = FArdaRHIResourceRef(Output.Get());
            Desc.mItems.push_back(Item);
            return Device->CreateBindingSet(Desc);
        };
        auto SetA = CreateSet(OutputA.mValue);
        auto SetB = CreateSet(OutputB.mValue);
        ASSERT_TRUE(SetA);
        ASSERT_TRUE(SetB);

        FArdaRHIShaderBundleDesc BundleDesc;
        BundleDesc.mMaxRecords = 2;
        BundleDesc.mbPersistent = true;
        BundleDesc.mDebugName = "Persistent compute shader bundle";
        auto Bundle = Device->CreateShaderBundle(BundleDesc);
        ASSERT_TRUE(Bundle) << Bundle.mStatus.mMessage.c_str();
        FArdaRHIShaderBundleRecord RecordA;
        RecordA.mComputePipeline = Pipeline.mValue;
        RecordA.mBindings.push_back(SetA.mValue);
        FArdaRHIShaderBundleRecord RecordB = RecordA;
        RecordB.mBindings = {SetB.mValue};
        ASSERT_TRUE(Device->SetShaderBundleRecords(
            Bundle.mValue, {RecordA, RecordB}));
        EXPECT_EQ(Bundle.mValue->GetRecordCount(), 2u);

        auto Commands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *OutputA.mValue, EArdaRHIResourceState::UnorderedAccess));
        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *OutputB.mValue, EArdaRHIResourceState::UnorderedAccess));
        ASSERT_TRUE(Commands.mValue->DispatchShaderBundle(*Bundle.mValue));
        ExpectBufferState(*Commands.mValue, *OutputA.mValue,
            EArdaRHIResourceState::UnorderedAccess);
        ExpectBufferState(*Commands.mValue, *OutputB.mValue,
            EArdaRHIResourceState::UnorderedAccess);
        eastl::vector<uint8_t> ReadbackA;
        eastl::vector<uint8_t> ReadbackB;
        ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(
            *OutputA.mValue, ReadbackA, 0, sizeof(uint32_t)));
        ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(
            *OutputB.mValue, ReadbackB, 0, sizeof(uint32_t)));
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        uint32_t ValueA = 0, ValueB = 0;
        ASSERT_EQ(ReadbackA.size(), sizeof(ValueA));
        ASSERT_EQ(ReadbackB.size(), sizeof(ValueB));
        std::memcpy(&ValueA, ReadbackA.data(), sizeof(ValueA));
        std::memcpy(&ValueB, ReadbackB.data(), sizeof(ValueB));
        EXPECT_EQ(ValueA, 0xA2DAu);
        EXPECT_EQ(ValueB, 0xA2DAu);
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyD3D12WorkGraphExecution()
    {
        using namespace arda::backend;
        using namespace arda::rhi;
        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = "native-d3d12";
        Configuration.mBackend = EArdaBackendType::D3D12;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        auto Device = GetDevice();
        ASSERT_TRUE(Device);
        if (Device->GetCapabilities().mWorkGraphTier ==
            EArdaRHIWorkGraphTier::None)
            GTEST_SKIP() << "D3D12 work graphs are unavailable.";
        ASSERT_TRUE(Device->QueryWorkGraphSupport());

        auto Shader = CreateExtendedShader(
            *Device, EArdaBackendType::D3D12,
            "ArdaWorkGraphTest", "WorkGraphMain",
            EArdaRHIShaderStage::WorkGraph);
        ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::WorkGraph;
        LayoutDesc.mItems.push_back(
            {0, 1, EArdaRHIBindingType::StructuredBufferUAV});
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(Layout) << Layout.mStatus.mMessage.c_str();
        FArdaRHIWorkGraphPipelineDesc PipelineDesc;
        PipelineDesc.mProgramName = "ArdaWorkGraphProgram";
        PipelineDesc.mEntryPoint = "WorkGraphMain";
        PipelineDesc.mShaders.push_back(Shader.mValue);
        PipelineDesc.mGlobalBindingLayouts.push_back(Layout.mValue);
        PipelineDesc.mMaxInputRecords = 4;
        PipelineDesc.mDebugName = "Native D3D12 work graph";
        auto Pipeline = Device->CreateWorkGraphPipeline(PipelineDesc);
        ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();

        FArdaRHIBufferDesc OutputDesc;
        OutputDesc.mByteSize = sizeof(uint32_t);
        OutputDesc.mStructureStride = sizeof(uint32_t);
        OutputDesc.mUsage = EArdaRHIBufferUsage::Structured |
            EArdaRHIBufferUsage::UnorderedAccess;
        auto Output = Device->CreateBuffer(OutputDesc);
        ASSERT_TRUE(Output);
        FArdaRHIBindingSetDesc SetDesc;
        SetDesc.mLayout = Layout.mValue;
        FArdaRHIBindingItem Item;
        Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
        Item.mResource = FArdaRHIResourceRef(Output.mValue.Get());
        SetDesc.mItems.push_back(Item);
        auto Set = Device->CreateBindingSet(SetDesc);
        ASSERT_TRUE(Set) << Set.mStatus.mMessage.c_str();

        const uint32_t InputRecord = 0xB16B00B5u;
        auto Commands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *Output.mValue, EArdaRHIResourceState::UnorderedAccess));
        ASSERT_TRUE(Commands.mValue->DispatchWorkGraph(
            *Pipeline.mValue, &InputRecord, 1, sizeof(InputRecord),
            {Set.mValue}));
        ExpectBufferState(*Commands.mValue, *Output.mValue,
            EArdaRHIResourceState::UnorderedAccess);
        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(
            *Output.mValue, Readback, 0, sizeof(uint32_t)));
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        uint32_t Value = 0;
        ASSERT_EQ(Readback.size(), sizeof(Value));
        std::memcpy(&Value, Readback.data(), sizeof(Value));
        EXPECT_EQ(Value, InputRecord);
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyMeshPipelineCapabilityAndExecution(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);

        auto MeshShader = CreateExtendedShader(
            *Device,
            Backend,
            "ArdaMeshPipelineTestMS",
            "MeshPipelineTestMS",
            EArdaRHIShaderStage::Mesh);
        auto PixelShader = CreateExtendedShader(
            *Device,
            Backend,
            "ArdaMeshPipelineTestPS",
            "MeshPipelineTestPS",
            EArdaRHIShaderStage::Pixel);
        ASSERT_TRUE(MeshShader) << MeshShader.mStatus.mMessage.c_str();
        ASSERT_TRUE(PixelShader) << PixelShader.mStatus.mMessage.c_str();

        FArdaRHIMeshletPipelineDesc PipelineDesc;
        PipelineDesc.mDebugName = "Mesh pipeline conformance";
        PipelineDesc.mMeshShader = MeshShader.mValue;
        PipelineDesc.mPixelShader = PixelShader.mValue;
        PipelineDesc.mColorFormats.push_back(EArdaRHIFormat::RGBA8UNorm);
        PipelineDesc.mRasterState.mCullMode = EArdaRHICullMode::None;
        PipelineDesc.mDepthStencilState.mbDepthTest = false;
        PipelineDesc.mDepthStencilState.mbDepthWrite = false;
        auto Pipeline = Device->CreateMeshletPipeline(PipelineDesc);
        if (Device->GetCapabilities().mMeshShaderTier ==
            EArdaRHIMeshShaderTier::None)
        {
            EXPECT_FALSE(Pipeline);
            EXPECT_EQ(Pipeline.mStatus.mCode, EArdaRHIResult::Unsupported);
            return;
        }
        ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();

        FArdaRHITextureDesc TargetDesc;
        TargetDesc.mDebugName = "Mesh pipeline native render target";
        TargetDesc.mWidth = 4;
        TargetDesc.mHeight = 4;
        TargetDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
        TargetDesc.mUsage = EArdaRHITextureUsage::RenderTarget;
        TargetDesc.mInitialState = EArdaRHIResourceState::RenderTarget;
        auto Target = Device->CreateTexture(TargetDesc);
        ASSERT_TRUE(Target) << Target.mStatus.mMessage.c_str();
        FArdaRHIFramebufferDesc FramebufferDesc;
        FramebufferDesc.mColorAttachments.push_back({ Target.mValue, {} });
        auto Framebuffer = Device->CreateFramebuffer(FramebufferDesc);
        ASSERT_TRUE(Framebuffer) << Framebuffer.mStatus.mMessage.c_str();

        FArdaRHIStagingTextureDesc ReadbackDesc;
        ReadbackDesc.mDebugName = "Mesh pipeline native readback";
        ReadbackDesc.mTexture = TargetDesc;
        ReadbackDesc.mCpuAccess = EArdaRHICpuAccess::Read;
        auto Readback = Device->CreateStagingTexture(ReadbackDesc);
        ASSERT_TRUE(Readback) << Readback.mStatus.mMessage.c_str();

        auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        Commands.mValue->SetAutomaticBarriers(false);
        ASSERT_TRUE(Commands.mValue->BeginTrackingTextureState(
            *Target.mValue,
            {},
            EArdaRHIResourceState::RenderTarget));
        ExpectTextureState(
            *Commands.mValue,
            *Target.mValue,
            {},
            EArdaRHIResourceState::RenderTarget);
        ASSERT_TRUE(Commands.mValue->ClearTexture(
            *Target.mValue, {}, { 0.f, 0.f, 0.f, 1.f }));
        ExpectTextureState(
            *Commands.mValue,
            *Target.mValue,
            {},
            EArdaRHIResourceState::RenderTarget);

        FArdaRHIMeshletState State;
        State.mPipeline = Pipeline.mValue;
        State.mFramebuffer = Framebuffer.mValue;
        State.mViewports.push_back({ 0.f, 4.f, 0.f, 4.f, 0.f, 1.f });
        State.mScissors.push_back({ 0, 4, 0, 4 });
        ASSERT_TRUE(Commands.mValue->SetMeshletState(State));
        ASSERT_TRUE(Commands.mValue->DispatchMesh(1, 1, 1));
        ExpectTextureState(
            *Commands.mValue,
            *Target.mValue,
            {},
            EArdaRHIResourceState::RenderTarget);

        FArdaRHITextureTransitionDesc ToReadback;
        ToReadback.mStateBefore = EArdaRHIResourceState::RenderTarget;
        ToReadback.mStateAfter = EArdaRHIResourceState::CopySource;
        ToReadback.mSourcePipelines = EArdaRHIPipeline::Graphics;
        ToReadback.mDestinationPipelines = EArdaRHIPipeline::Copy;
        ASSERT_TRUE(Commands.mValue->TransitionTexture(
            *Target.mValue, ToReadback));
        ExpectTextureState(
            *Commands.mValue,
            *Target.mValue,
            {},
            EArdaRHIResourceState::CopySource);
        FArdaRHITextureSlice Slice;
        Slice.mWidth = 4;
        Slice.mHeight = 4;
        Slice.mDepth = 1;
        ASSERT_TRUE(Commands.mValue->CopyTextureToStaging(
            *Readback.mValue, Slice, *Target.mValue, Slice));
        ExpectTextureState(
            *Commands.mValue,
            *Target.mValue,
            {},
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());

        auto Mapping = Device->MapStagingTexture(
            Readback.mValue, Slice, EArdaRHICpuAccess::Read);
        ASSERT_TRUE(Mapping) << Mapping.mStatus.mMessage.c_str();
        ASSERT_GE(Mapping.mValue.mRowPitch, 16u);
        const auto* Center = static_cast<const uint8_t*>(
            Mapping.mValue.mData) +
            2u * Mapping.mValue.mRowPitch + 2u * 4u;
        EXPECT_LE(Center[0], 1u);
        EXPECT_GE(Center[1], 254u);
        EXPECT_LE(Center[2], 1u);
        EXPECT_EQ(Center[3], 255u);
        ASSERT_TRUE(Device->UnmapStagingTexture(Readback.mValue));
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyQueueBreadth(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);

        const FArdaRHICapabilities& Capabilities =
            Device->GetCapabilities();
        if (Backend == EArdaBackendType::D3D12)
        {
            EXPECT_TRUE(Capabilities.mQueues.mbCompute);
            EXPECT_TRUE(Capabilities.mQueues.mbCopy);
        }

        if (Capabilities.mQueues.mbCopy)
        {
            FArdaRHIBufferDesc BufferDesc;
            BufferDesc.mByteSize = sizeof(uint32_t);
            BufferDesc.mInitialState = EArdaRHIResourceState::Common;
            BufferDesc.mDebugName = "Queue breadth source";
            auto Source = Device->CreateBuffer(BufferDesc);
            BufferDesc.mDebugName = "Queue breadth destination";
            auto Destination = Device->CreateBuffer(BufferDesc);
            ASSERT_TRUE(Source);
            ASSERT_TRUE(Destination);

            auto CopyCommands = Device->CreateCommandList(
                EArdaRHIQueueType::Copy);
            ASSERT_TRUE(CopyCommands);
            ASSERT_TRUE(CopyCommands.mValue->Open());
            const uint32_t Expected = 0xC0A1C0A1u;
            ASSERT_TRUE(CopyCommands.mValue->WriteBuffer(
                *Source.mValue, &Expected, sizeof(Expected)));
            ExpectBufferState(
                *CopyCommands.mValue,
                *Source.mValue,
                EArdaRHIResourceState::Common);
            FArdaRHIBufferTransitionDesc SourceTransition;
            SourceTransition.mStateBefore = EArdaRHIResourceState::Common;
            SourceTransition.mStateAfter = EArdaRHIResourceState::CopySource;
            SourceTransition.mSourcePipelines = EArdaRHIPipeline::Graphics;
            SourceTransition.mDestinationPipelines = EArdaRHIPipeline::Copy;
            ASSERT_TRUE(CopyCommands.mValue->TransitionBuffer(
                *Source.mValue, SourceTransition));
            FArdaRHIBufferTransitionDesc DestinationTransition;
            DestinationTransition.mStateBefore = EArdaRHIResourceState::Common;
            DestinationTransition.mStateAfter = EArdaRHIResourceState::CopyDest;
            DestinationTransition.mSourcePipelines = EArdaRHIPipeline::Graphics;
            DestinationTransition.mDestinationPipelines = EArdaRHIPipeline::Copy;
            ASSERT_TRUE(CopyCommands.mValue->TransitionBuffer(
                *Destination.mValue, DestinationTransition));
            ExpectBufferState(
                *CopyCommands.mValue,
                *Source.mValue,
                EArdaRHIResourceState::CopySource);
            ExpectBufferState(
                *CopyCommands.mValue,
                *Destination.mValue,
                EArdaRHIResourceState::CopyDest);
            ASSERT_TRUE(CopyCommands.mValue->CopyBuffer(
                *Destination.mValue,
                0,
                *Source.mValue,
                0,
                sizeof(Expected)));
            ExpectBufferState(
                *CopyCommands.mValue,
                *Source.mValue,
                EArdaRHIResourceState::CopySource);
            ExpectBufferState(
                *CopyCommands.mValue,
                *Destination.mValue,
                EArdaRHIResourceState::CopyDest);
            ASSERT_TRUE(CopyCommands.mValue->Close());
            auto Submission = Device->ExecuteCommandList(CopyCommands.mValue);
            ASSERT_TRUE(Submission);
            ASSERT_TRUE(Device->QueueWait(
                EArdaRHIQueueType::Graphics,
                EArdaRHIQueueType::Copy,
                Submission.mValue));

            auto ReadbackCommands = Device->CreateCommandList(
                EArdaRHIQueueType::Graphics);
            ASSERT_TRUE(ReadbackCommands);
            ASSERT_TRUE(ReadbackCommands.mValue->Open());
            eastl::vector<uint8_t> Readback;
            ASSERT_TRUE(ReadbackCommands.mValue->CopyBufferDeviceToHost(
                *Destination.mValue,
                Readback,
                0,
                sizeof(Expected)));
            ExpectBufferState(
                *ReadbackCommands.mValue,
                *Destination.mValue,
                EArdaRHIResourceState::CopySource);
            ASSERT_TRUE(ReadbackCommands.mValue->Close());
            ASSERT_TRUE(Device->ExecuteCommandList(ReadbackCommands.mValue));
            ASSERT_EQ(Readback.size(), sizeof(Expected));
            uint32_t Actual = 0;
            std::memcpy(&Actual, Readback.data(), sizeof(Actual));
            EXPECT_EQ(Actual, Expected);
        }
        else
        {
            auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Copy);
            EXPECT_FALSE(Commands);
            EXPECT_EQ(Commands.mStatus.mCode, EArdaRHIResult::Unsupported);
        }

        if (Capabilities.mQueues.mbCompute)
        {
            FArdaRHIBufferDesc OutputDesc;
            OutputDesc.mDebugName = "Compute queue breadth output";
            OutputDesc.mByteSize = sizeof(uint32_t);
            OutputDesc.mStructureStride = sizeof(uint32_t);
            OutputDesc.mUsage = EArdaRHIBufferUsage::Structured |
                EArdaRHIBufferUsage::UnorderedAccess;
            OutputDesc.mInitialState = EArdaRHIResourceState::Common;
            auto Output = Device->CreateBuffer(OutputDesc);
            ASSERT_TRUE(Output);
            auto Shader = CreateExtendedComputeShader(*Device, Backend);
            ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
            FArdaRHIBindingLayoutDesc LayoutDesc;
            LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
            LayoutDesc.mItems.push_back(
                { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
            auto Layout = Device->CreateBindingLayout(LayoutDesc);
            ASSERT_TRUE(Layout);
            FArdaRHIBindingSetDesc SetDesc;
            SetDesc.mLayout = Layout.mValue;
            FArdaRHIBindingItem Item;
            Item.mSlot = 0;
            Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
            Item.mResource =
                TArdaRHIRef<IArdaRHIResource>(Output.mValue.Get());
            SetDesc.mItems.push_back(Item);
            auto BindingSet = Device->CreateBindingSet(SetDesc);
            ASSERT_TRUE(BindingSet);
            FArdaRHIComputePipelineDesc PipelineDesc;
            PipelineDesc.mComputeShader = Shader.mValue;
            PipelineDesc.mBindingLayouts.push_back(Layout.mValue);
            auto Pipeline = Device->CreateComputePipeline(PipelineDesc);
            ASSERT_TRUE(Pipeline);

            auto ComputeCommands = Device->CreateCommandList(
                EArdaRHIQueueType::Compute);
            ASSERT_TRUE(ComputeCommands);
            ASSERT_TRUE(ComputeCommands.mValue->Open());
            ASSERT_TRUE(ComputeCommands.mValue->SetBufferState(
                *Output.mValue,
                EArdaRHIResourceState::UnorderedAccess));
            ExpectBufferState(
                *ComputeCommands.mValue,
                *Output.mValue,
                EArdaRHIResourceState::UnorderedAccess);
            FArdaRHIComputeState State;
            State.mPipeline = Pipeline.mValue;
            State.mBindings.push_back(BindingSet.mValue);
            ASSERT_TRUE(ComputeCommands.mValue->SetComputeState(State));
            ComputeCommands.mValue->Dispatch(1, 1, 1);
            eastl::vector<uint8_t> Readback;
            ASSERT_TRUE(ComputeCommands.mValue->CopyBufferDeviceToHost(
                *Output.mValue,
                Readback,
                0,
                sizeof(uint32_t)));
            ExpectBufferState(
                *ComputeCommands.mValue,
                *Output.mValue,
                EArdaRHIResourceState::CopySource);
            ASSERT_TRUE(ComputeCommands.mValue->Close());
            ASSERT_TRUE(Device->ExecuteCommandList(ComputeCommands.mValue));
            ASSERT_EQ(Readback.size(), sizeof(uint32_t));
            uint32_t Value = 0;
            std::memcpy(&Value, Readback.data(), sizeof(Value));
            EXPECT_EQ(Value, 0xA2DAu);
        }
        else
        {
            auto Commands = Device->CreateCommandList(
                EArdaRHIQueueType::Compute);
            EXPECT_FALSE(Commands);
            EXPECT_EQ(Commands.mStatus.mCode, EArdaRHIResult::Unsupported);
        }
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyRayTracingPipelineCapabilityAndExecution(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);

        auto RayGeneration = CreateExtendedShader(
            *Device,
            Backend,
            "ArdaRayTracingTest",
            "RayGen",
            EArdaRHIShaderStage::RayGeneration);
        ASSERT_TRUE(RayGeneration)
            << RayGeneration.mStatus.mMessage.c_str();
        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::AllRayTracing;
        LayoutDesc.mItems.push_back(
            { 0, 1, EArdaRHIBindingType::StructuredBufferUAV });
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(Layout) << Layout.mStatus.mMessage.c_str();

        FArdaRHIRayTracingPipelineDesc PipelineDesc;
        PipelineDesc.mDebugName = "Ray-generation pipeline conformance";
        PipelineDesc.mShaders.push_back(
            { "RayGen", RayGeneration.mValue, {} });
        PipelineDesc.mGlobalBindingLayouts.push_back(Layout.mValue);
        PipelineDesc.mMaxPayloadSize = sizeof(uint32_t);
        PipelineDesc.mMaxRecursionDepth = 1;
        auto Pipeline = Device->CreateRayTracingPipeline(PipelineDesc);
        if (!Device->GetCapabilities().mRayTracing.mbPipelineShaders)
        {
            EXPECT_FALSE(Pipeline);
            EXPECT_EQ(Pipeline.mStatus.mCode, EArdaRHIResult::Unsupported);
            return;
        }
        EXPECT_GT(
            Device->GetCapabilities().mRayTracing.
                mMaxRayDispatchInvocations,
            0u);
        ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();

        FArdaRHIBufferDesc OutputDesc;
        OutputDesc.mDebugName = "Ray-generation native output";
        OutputDesc.mByteSize = sizeof(uint32_t);
        OutputDesc.mStructureStride = sizeof(uint32_t);
        OutputDesc.mUsage = EArdaRHIBufferUsage::Structured |
            EArdaRHIBufferUsage::UnorderedAccess;
        OutputDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Output = Device->CreateBuffer(OutputDesc);
        ASSERT_TRUE(Output);
        FArdaRHIBindingSetDesc SetDesc;
        SetDesc.mLayout = Layout.mValue;
        FArdaRHIBindingItem Item;
        Item.mSlot = 0;
        Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
        Item.mResource =
            TArdaRHIRef<IArdaRHIResource>(Output.mValue.Get());
        SetDesc.mItems.push_back(Item);
        auto BindingSet = Device->CreateBindingSet(SetDesc);
        ASSERT_TRUE(BindingSet) << BindingSet.mStatus.mMessage.c_str();

        FArdaRHIShaderTableDesc TableDesc;
        TableDesc.mDebugName = "Ray-generation shader table conformance";
        TableDesc.mMaxEntries = 1;
        auto ShaderTable = Device->CreateShaderTable(
            Pipeline.mValue, TableDesc);
        ASSERT_TRUE(ShaderTable) << ShaderTable.mStatus.mMessage.c_str();
        ASSERT_TRUE(Device->SetShaderTableRayGeneration(
            ShaderTable.mValue, "RayGen"));
        EXPECT_EQ(ShaderTable.mValue->GetEntryCount(), 1u);

        auto Commands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *Output.mValue,
            EArdaRHIResourceState::UnorderedAccess));
        ExpectBufferState(
            *Commands.mValue,
            *Output.mValue,
            EArdaRHIResourceState::UnorderedAccess);
        FArdaRHIRayTracingState State;
        State.mShaderTable = ShaderTable.mValue;
        State.mBindings.push_back(BindingSet.mValue);
        ASSERT_TRUE(Commands.mValue->SetRayTracingState(State));
        const FArdaRHIStatus OversizedDispatch =
            Commands.mValue->DispatchRays(UINT32_MAX, 2, 1);
        EXPECT_FALSE(OversizedDispatch);
        EXPECT_EQ(
            OversizedDispatch.mCode,
            EArdaRHIResult::InvalidArgument);
        ASSERT_TRUE(Commands.mValue->DispatchRays(1, 1, 1));
        ExpectBufferState(
            *Commands.mValue,
            *Output.mValue,
            EArdaRHIResourceState::UnorderedAccess);
        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(
            *Output.mValue,
            Readback,
            0,
            sizeof(uint32_t)));
        ExpectBufferState(
            *Commands.mValue,
            *Output.mValue,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_EQ(Readback.size(), sizeof(uint32_t));
        uint32_t Value = 0;
        std::memcpy(&Value, Readback.data(), sizeof(Value));
        EXPECT_EQ(Value, 0xA11CEu);
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyAccelerationStructureLifecycleAndStateParity(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();

        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        const auto& Ray = Device->GetCapabilities().mRayTracing;
        if (!Ray.mbAccelerationStructures)
            GTEST_SKIP() << "Native acceleration structures unavailable.";
        EXPECT_TRUE(Ray.mbBottomLevel);
        EXPECT_TRUE(Ray.mbTopLevel);
        EXPECT_TRUE(Ray.mbBuildUpdate);
        EXPECT_TRUE(Ray.mbCompaction);
        EXPECT_GT(Ray.mAccelerationStructureAlignment, 0u);

        const float Vertices[9] = {
            -1.f, -1.f, 0.f,
             0.f,  1.f, 0.f,
             1.f, -1.f, 0.f};
        FArdaRHIBufferDesc VertexDesc;
        VertexDesc.mByteSize = sizeof(Vertices);
        VertexDesc.mUsage = EArdaRHIBufferUsage::Vertex |
            EArdaRHIBufferUsage::AccelStructBuildInput;
        VertexDesc.mCpuAccess = EArdaRHICpuAccess::Write;
        VertexDesc.mInitialState =
            EArdaRHIResourceState::AccelStructBuildInput;
        VertexDesc.mDebugName = "AS parity vertices";
        auto VertexBuffer = Device->CreateBuffer(VertexDesc);
        ASSERT_TRUE(VertexBuffer) << VertexBuffer.mStatus.mMessage.c_str();

        FArdaRHIRayTracingGeometryDesc Geometry;
        Geometry.mType = EArdaRHIRayTracingGeometryType::Triangles;
        Geometry.mFlags = EArdaRHIRayTracingGeometryFlags::Opaque;
        Geometry.mVertexOrAABBBuffer = VertexBuffer.mValue;
        Geometry.mVertexFormat = EArdaRHIFormat::RGB32Float;
        Geometry.mVertexOrAABBCount = 3;
        Geometry.mStride = sizeof(float) * 3;

        FArdaRHIAccelStructDesc BlasDesc;
        BlasDesc.mBottomLevelGeometries.push_back(Geometry);
        BlasDesc.mBuildFlags =
            EArdaRHIAccelStructBuildFlags::AllowUpdate |
            EArdaRHIAccelStructBuildFlags::AllowCompaction |
            EArdaRHIAccelStructBuildFlags::PreferFastTrace;
        BlasDesc.mDebugName = "AS parity BLAS";
        const auto BlasRequirements =
            Device->GetAccelStructBuildMemoryRequirements(BlasDesc);
        ASSERT_TRUE(BlasRequirements)
            << BlasRequirements.mStatus.mMessage.c_str();
        EXPECT_GT(BlasRequirements.mValue.mResultSize, 0u);
        EXPECT_GT(BlasRequirements.mValue.mBuildScratchSize, 0u);
        auto Blas = Device->CreateAccelStruct(BlasDesc);
        ASSERT_TRUE(Blas) << Blas.mStatus.mMessage.c_str();
        EXPECT_NE(Blas.mValue->GetDeviceAddress(), 0u);
        EXPECT_EQ(Blas.mValue->GetBuildState(),
            EArdaRHIAccelStructBuildState::Unbuilt);

        FArdaRHIAccelStructDesc TlasDesc;
        TlasDesc.mbTopLevel = true;
        TlasDesc.mTopLevelMaxInstances = 1;
        TlasDesc.mBuildFlags = EArdaRHIAccelStructBuildFlags::AllowUpdate |
            EArdaRHIAccelStructBuildFlags::AllowCompaction |
            EArdaRHIAccelStructBuildFlags::PreferFastTrace;
        TlasDesc.mDebugName = "AS parity TLAS";
        auto Tlas = Device->CreateAccelStruct(TlasDesc);
        ASSERT_TRUE(Tlas) << Tlas.mStatus.mMessage.c_str();

        auto Build = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Build);
        ASSERT_TRUE(Build.mValue->Open());
        ASSERT_TRUE(Build.mValue->WriteBuffer(
            *VertexBuffer.mValue, Vertices, sizeof(Vertices), 0));
        ASSERT_TRUE(Build.mValue->BuildBottomLevelAccelStruct(
            *Blas.mValue, {Geometry}, BlasDesc.mBuildFlags));
        auto BlasState = Build.mValue->QueryAccelStructState(*Blas.mValue);
        ASSERT_TRUE(BlasState) << BlasState.mStatus.mMessage.c_str();
        EXPECT_TRUE(BlasState.mValue.IsConsistent());
        EXPECT_TRUE(BlasState.mValue.mNative.mbNativeCompatible);
        EXPECT_EQ(BlasState.mValue.mFacadeState,
            EArdaRHIResourceState::AccelStructRead);
        FArdaRHIRayTracingInstanceDesc Instance;
        Instance.mBottomLevelAccelStruct = Blas.mValue;
        ASSERT_TRUE(Build.mValue->BuildTopLevelAccelStruct(
            *Tlas.mValue, {Instance}, TlasDesc.mBuildFlags));
        auto TlasState = Build.mValue->QueryAccelStructState(*Tlas.mValue);
        ASSERT_TRUE(TlasState) << TlasState.mStatus.mMessage.c_str();
        EXPECT_TRUE(TlasState.mValue.IsConsistent());
        EXPECT_TRUE(TlasState.mValue.mNative.mbNativeCompatible);
        ASSERT_TRUE(Build.mValue->Close());
        const auto BuiltSubmission = Device->ExecuteCommandList(Build.mValue);
        ASSERT_TRUE(BuiltSubmission)
            << BuiltSubmission.mStatus.mMessage.c_str();
        ASSERT_TRUE(Device->WaitForIdle());
        EXPECT_EQ(Blas.mValue->GetBuildState(),
            EArdaRHIAccelStructBuildState::Built);
        EXPECT_EQ(Tlas.mValue->GetBuildState(),
            EArdaRHIAccelStructBuildState::Built);

        auto Update = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Update);
        ASSERT_TRUE(Update.mValue->Open());
        ASSERT_TRUE(Update.mValue->BuildBottomLevelAccelStruct(
            *Blas.mValue, {Geometry},
            BlasDesc.mBuildFlags |
                EArdaRHIAccelStructBuildFlags::PerformUpdate));
        BlasState = Update.mValue->QueryAccelStructState(*Blas.mValue);
        ASSERT_TRUE(BlasState);
        EXPECT_TRUE(BlasState.mValue.IsConsistent());
        EXPECT_EQ(BlasState.mValue.mFacadeState,
            EArdaRHIResourceState::AccelStructRead);
        ASSERT_TRUE(Update.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Update.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        EXPECT_EQ(Blas.mValue->GetBuildState(),
            EArdaRHIAccelStructBuildState::Updated);

        const auto CompactedSize =
            Device->GetAccelStructCompactedSize(Blas.mValue);
        ASSERT_TRUE(CompactedSize)
            << CompactedSize.mStatus.mMessage.c_str();
        EXPECT_GT(CompactedSize.mValue, 0u);
        EXPECT_LE(CompactedSize.mValue,
            BlasRequirements.mValue.mResultSize);
        FArdaRHIAccelStructDesc CompactDesc = BlasDesc;
        CompactDesc.mResultSizeOverride = CompactedSize.mValue;
        CompactDesc.mDebugName = "AS parity compact BLAS";
        auto CompactBlas = Device->CreateAccelStruct(CompactDesc);
        ASSERT_TRUE(CompactBlas)
            << CompactBlas.mStatus.mMessage.c_str();
        auto Compact = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Compact);
        ASSERT_TRUE(Compact.mValue->Open());
        ASSERT_TRUE(Compact.mValue->CompactAccelStruct(
            *CompactBlas.mValue, *Blas.mValue));
        const auto CompactState =
            Compact.mValue->QueryAccelStructState(*CompactBlas.mValue);
        ASSERT_TRUE(CompactState);
        EXPECT_TRUE(CompactState.mValue.IsConsistent());
        EXPECT_EQ(CompactState.mValue.mFacadeState,
            EArdaRHIResourceState::AccelStructRead);
        ASSERT_TRUE(Compact.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Compact.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        EXPECT_TRUE(CompactBlas.mValue->IsCompacted());
        EXPECT_EQ(CompactBlas.mValue->GetBuildState(),
            EArdaRHIAccelStructBuildState::Compacted);
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyRayTracingSceneHitGroupsLocalArgumentsAndIndirect(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        const auto& Ray = Device->GetCapabilities().mRayTracing;
        if (!Ray.mbPipelineShaders || !Ray.mbAccelerationStructures ||
            !Ray.mbIndirectDispatch || !Ray.mbLocalShaderTableArguments)
        {
            GTEST_SKIP() <<
                "Native scene tracing, indirect dispatch, or local SBT arguments are unavailable.";
        }

        auto RayGeneration = CreateExtendedShader(
            *Device, Backend, "ArdaRayTracingTest", "KnownSceneRayGen",
            EArdaRHIShaderStage::RayGeneration);
        auto Miss = CreateExtendedShader(
            *Device, Backend, "ArdaRayTracingTest", "KnownMiss",
            EArdaRHIShaderStage::Miss);
        auto ClosestHit = CreateExtendedShader(
            *Device, Backend, "ArdaRayTracingTest", "KnownClosestHit",
            EArdaRHIShaderStage::ClosestHit);
        ASSERT_TRUE(RayGeneration)
            << RayGeneration.mStatus.mMessage.c_str();
        ASSERT_TRUE(Miss) << Miss.mStatus.mMessage.c_str();
        ASSERT_TRUE(ClosestHit) << ClosestHit.mStatus.mMessage.c_str();

        const float Vertices[9] = {
            -1.f, -1.f, 0.f,
             0.f,  1.f, 0.f,
             1.f, -1.f, 0.f};
        FArdaRHIBufferDesc VertexDesc;
        VertexDesc.mByteSize = sizeof(Vertices);
        VertexDesc.mUsage = EArdaRHIBufferUsage::Vertex |
            EArdaRHIBufferUsage::AccelStructBuildInput;
        VertexDesc.mCpuAccess = EArdaRHICpuAccess::Write;
        VertexDesc.mInitialState =
            EArdaRHIResourceState::AccelStructBuildInput;
        VertexDesc.mDebugName = "Known-result ray-scene vertices";
        auto VertexBuffer = Device->CreateBuffer(VertexDesc);
        ASSERT_TRUE(VertexBuffer)
            << VertexBuffer.mStatus.mMessage.c_str();

        FArdaRHIRayTracingGeometryDesc Geometry;
        Geometry.mType = EArdaRHIRayTracingGeometryType::Triangles;
        Geometry.mFlags = EArdaRHIRayTracingGeometryFlags::Opaque;
        Geometry.mVertexOrAABBBuffer = VertexBuffer.mValue;
        Geometry.mVertexFormat = EArdaRHIFormat::RGB32Float;
        Geometry.mVertexOrAABBCount = 3;
        Geometry.mStride = sizeof(float) * 3;
        FArdaRHIAccelStructDesc BlasDesc;
        BlasDesc.mBottomLevelGeometries.push_back(Geometry);
        BlasDesc.mBuildFlags =
            EArdaRHIAccelStructBuildFlags::AllowUpdate |
            EArdaRHIAccelStructBuildFlags::AllowCompaction |
            EArdaRHIAccelStructBuildFlags::PreferFastTrace;
        BlasDesc.mDebugName = "Known-result ray-scene BLAS";
        auto Blas = Device->CreateAccelStruct(BlasDesc);
        ASSERT_TRUE(Blas) << Blas.mStatus.mMessage.c_str();
        FArdaRHIAccelStructDesc TlasDesc;
        TlasDesc.mbTopLevel = true;
        TlasDesc.mTopLevelMaxInstances = 1;
        TlasDesc.mBuildFlags =
            EArdaRHIAccelStructBuildFlags::AllowUpdate |
            EArdaRHIAccelStructBuildFlags::PreferFastTrace;
        TlasDesc.mDebugName = "Known-result ray-scene TLAS";
        auto Tlas = Device->CreateAccelStruct(TlasDesc);
        ASSERT_TRUE(Tlas) << Tlas.mStatus.mMessage.c_str();

        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::AllRayTracing;
        LayoutDesc.mItems.push_back(
            {0, 1, EArdaRHIBindingType::RayTracingAccelStruct});
        LayoutDesc.mItems.push_back(
            {0, 1, EArdaRHIBindingType::StructuredBufferUAV});
        LayoutDesc.mDebugName = "Known-result ray-scene globals";
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(Layout) << Layout.mStatus.mMessage.c_str();

        FArdaRHIRayTracingPipelineDesc PipelineDesc;
        PipelineDesc.mDebugName =
            "Known-result hit/miss ray tracing pipeline";
        PipelineDesc.mShaders.push_back(
            {"KnownSceneRayGen", RayGeneration.mValue, {}});
        PipelineDesc.mShaders.push_back(
            {"KnownMiss", Miss.mValue, {}});
        FArdaRHIRayTracingHitGroupDesc HitGroup;
        HitGroup.mExportName = "KnownHitGroup";
        HitGroup.mClosestHitShader = ClosestHit.mValue;
        PipelineDesc.mHitGroups.push_back(HitGroup);
        PipelineDesc.mGlobalBindingLayouts.push_back(Layout.mValue);
        PipelineDesc.mMaxPayloadSize = sizeof(uint32_t);
        PipelineDesc.mMaxRecursionDepth = 1;
        auto Pipeline = Device->CreateRayTracingPipeline(PipelineDesc);
        ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();

        FArdaRHIBufferDesc OutputDesc;
        OutputDesc.mByteSize = sizeof(uint32_t) * 2u;
        OutputDesc.mStructureStride = sizeof(uint32_t);
        OutputDesc.mUsage = EArdaRHIBufferUsage::Structured |
            EArdaRHIBufferUsage::UnorderedAccess;
        OutputDesc.mInitialState = EArdaRHIResourceState::Common;
        OutputDesc.mDebugName = "Known-result ray-scene output";
        auto Output = Device->CreateBuffer(OutputDesc);
        ASSERT_TRUE(Output) << Output.mStatus.mMessage.c_str();
        FArdaRHIBindingSetDesc SetDesc;
        SetDesc.mLayout = Layout.mValue;
        FArdaRHIBindingItem SceneItem;
        SceneItem.mSlot = 0;
        SceneItem.mType = EArdaRHIBindingType::RayTracingAccelStruct;
        SceneItem.mResource =
            TArdaRHIRef<IArdaRHIResource>(Tlas.mValue.Get());
        SetDesc.mItems.push_back(SceneItem);
        FArdaRHIBindingItem OutputItem;
        OutputItem.mSlot = 0;
        OutputItem.mType = EArdaRHIBindingType::StructuredBufferUAV;
        OutputItem.mResource =
            TArdaRHIRef<IArdaRHIResource>(Output.mValue.Get());
        SetDesc.mItems.push_back(OutputItem);
        auto BindingSet = Device->CreateBindingSet(SetDesc);
        ASSERT_TRUE(BindingSet)
            << BindingSet.mStatus.mMessage.c_str();

        FArdaRHIShaderTableDesc TableDesc;
        TableDesc.mMaxEntries = 3;
        TableDesc.mMaxLocalArgumentBytes = sizeof(uint32_t);
        TableDesc.mbPersistent = true;
        TableDesc.mDebugName =
            "Known-result ray-scene shader table";
        auto ShaderTable = Device->CreateShaderTable(
            Pipeline.mValue, TableDesc);
        ASSERT_TRUE(ShaderTable)
            << ShaderTable.mStatus.mMessage.c_str();
        const auto WriteRecord = [&Device, &ShaderTable](
            uint32_t Index,
            EArdaRHIShaderTableRecordType Type,
            const char* Export,
            uint32_t LocalValue)
        {
            FArdaRHIShaderTableRecordDesc Record;
            Record.mRecordIndex = Index;
            Record.mType = Type;
            Record.mExportName = Export;
            Record.mLocalArguments.resize(sizeof(LocalValue));
            std::memcpy(
                Record.mLocalArguments.data(),
                &LocalValue,
                sizeof(LocalValue));
            return Device->SetShaderTableRecord(
                ShaderTable.mValue, Record);
        };
        ASSERT_TRUE(WriteRecord(
            0, EArdaRHIShaderTableRecordType::RayGeneration,
            "KnownSceneRayGen", 0x11111111u));
        ASSERT_TRUE(WriteRecord(
            1, EArdaRHIShaderTableRecordType::Miss,
            "KnownMiss", 0x22222222u));
        ASSERT_TRUE(WriteRecord(
            2, EArdaRHIShaderTableRecordType::HitGroup,
            "KnownHitGroup", 0x33333333u));
        ASSERT_TRUE(Device->CommitShaderTable(ShaderTable.mValue));
        EXPECT_EQ(ShaderTable.mValue->GetEntryCount(), 3u);

        FArdaRHIShaderTableRecordDesc OversizedRecord;
        OversizedRecord.mRecordIndex = 2;
        OversizedRecord.mType = EArdaRHIShaderTableRecordType::HitGroup;
        OversizedRecord.mExportName = "KnownHitGroup";
        OversizedRecord.mLocalArguments.resize(sizeof(uint64_t));
        const auto OversizedStatus = Device->SetShaderTableRecord(
            ShaderTable.mValue, OversizedRecord);
        EXPECT_FALSE(OversizedStatus);
        EXPECT_EQ(OversizedStatus.mCode, EArdaRHIResult::InvalidArgument);

        const uint32_t Dimensions[3] = {2, 1, 1};
        FArdaRHIBufferDesc IndirectDesc;
        IndirectDesc.mByteSize = sizeof(Dimensions);
        IndirectDesc.mUsage = EArdaRHIBufferUsage::Indirect;
        IndirectDesc.mInitialState = EArdaRHIResourceState::Common;
        IndirectDesc.mDebugName = "Portable indirect ray dimensions";
        auto Indirect = Device->CreateBuffer(IndirectDesc);
        ASSERT_TRUE(Indirect) << Indirect.mStatus.mMessage.c_str();

        auto Commands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ASSERT_TRUE(Commands.mValue->WriteBuffer(
            *VertexBuffer.mValue, Vertices, sizeof(Vertices), 0));
        ASSERT_TRUE(Commands.mValue->BuildBottomLevelAccelStruct(
            *Blas.mValue, {Geometry}, BlasDesc.mBuildFlags));
        FArdaRHIRayTracingInstanceDesc Instance;
        Instance.mBottomLevelAccelStruct = Blas.mValue;
        ASSERT_TRUE(Commands.mValue->BuildTopLevelAccelStruct(
            *Tlas.mValue, {Instance}, TlasDesc.mBuildFlags));
        const auto BlasState =
            Commands.mValue->QueryAccelStructState(*Blas.mValue);
        const auto TlasState =
            Commands.mValue->QueryAccelStructState(*Tlas.mValue);
        ASSERT_TRUE(BlasState);
        ASSERT_TRUE(TlasState);
        EXPECT_TRUE(BlasState.mValue.IsConsistent());
        EXPECT_TRUE(TlasState.mValue.IsConsistent());
        EXPECT_EQ(BlasState.mValue.mFacadeState,
            EArdaRHIResourceState::AccelStructRead);
        EXPECT_EQ(TlasState.mValue.mFacadeState,
            EArdaRHIResourceState::AccelStructRead);
        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *Output.mValue,
            EArdaRHIResourceState::UnorderedAccess));
        ASSERT_TRUE(Commands.mValue->WriteBuffer(
            *Indirect.mValue,
            Dimensions,
            sizeof(Dimensions),
            0));
        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *Indirect.mValue,
            EArdaRHIResourceState::IndirectArgument));
        ExpectBufferState(
            *Commands.mValue,
            *Indirect.mValue,
            EArdaRHIResourceState::IndirectArgument);
        FArdaRHIRayTracingState State;
        State.mShaderTable = ShaderTable.mValue;
        State.mBindings.push_back(BindingSet.mValue);
        ASSERT_TRUE(Commands.mValue->SetRayTracingState(State));
        ASSERT_TRUE(Commands.mValue->DispatchRaysIndirect(
            *Indirect.mValue));
        ExpectBufferState(
            *Commands.mValue,
            *Indirect.mValue,
            EArdaRHIResourceState::IndirectArgument);
        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(
            *Output.mValue,
            Readback,
            0,
            OutputDesc.mByteSize));
        ExpectBufferState(
            *Commands.mValue,
            *Output.mValue,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(Commands.mValue->Close());
        const auto Submission =
            Device->ExecuteCommandList(Commands.mValue);
        ASSERT_TRUE(Submission)
            << Submission.mStatus.mMessage.c_str();
        ASSERT_EQ(Readback.size(), OutputDesc.mByteSize);
        uint32_t Actual[2]{};
        std::memcpy(Actual, Readback.data(), sizeof(Actual));
        EXPECT_EQ(Actual[0], 0xC105E57u);
        EXPECT_EQ(Actual[1], 0xB055u);
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyVulkanOpacityMicromapLifecycleAndStateParity()
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = "native-vulkan";
        Configuration.mBackend = EArdaBackendType::Vulkan;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        auto Device = GetDevice();
        ASSERT_TRUE(Device);
        if (!Device->GetCapabilities().mRayTracing.mbOpacityMicromaps)
            GTEST_SKIP() << "VK_EXT_opacity_micromap is unavailable.";

        FArdaRHIBufferDesc InputDesc;
        InputDesc.mByteSize = 256;
        InputDesc.mUsage =
            EArdaRHIBufferUsage::OpacityMicromapBuildInput;
        InputDesc.mCpuAccess = EArdaRHICpuAccess::Write;
        InputDesc.mInitialState =
            EArdaRHIResourceState::OpacityMicromapBuildInput;
        InputDesc.mDebugName = "Opacity micromap encoded data";
        auto Input = Device->CreateBuffer(InputDesc);
        ASSERT_TRUE(Input) << Input.mStatus.mMessage.c_str();
        InputDesc.mDebugName = "Opacity micromap triangle descriptors";
        auto Triangles = Device->CreateBuffer(InputDesc);
        ASSERT_TRUE(Triangles) << Triangles.mStatus.mMessage.c_str();

        FArdaRHIOpacityMicromapDesc Desc;
        Desc.mCounts.push_back(
            {1, 0, EArdaRHIOpacityMicromapFormat::TwoState});
        Desc.mInputBuffer = Input.mValue;
        Desc.mPerMicromapDescBuffer = Triangles.mValue;
        Desc.mFlags = EArdaRHIOpacityMicromapBuildFlags::FastTrace |
            EArdaRHIOpacityMicromapBuildFlags::AllowCompaction;
        Desc.mDebugName = "Native Vulkan opacity micromap";
        auto Micromap = Device->CreateOpacityMicromap(Desc);
        ASSERT_TRUE(Micromap) << Micromap.mStatus.mMessage.c_str();
        EXPECT_NE(Micromap.mValue->GetPhysicalIdentity(), nullptr);
        EXPECT_NE(Micromap.mValue->GetDeviceAddress(), 0u);
        EXPECT_EQ(Micromap.mValue->GetBuildState(),
            EArdaRHIAccelStructBuildState::Unbuilt);

        struct FMicromapTriangle
        {
            uint32_t mDataOffset;
            uint16_t mSubdivisionLevel;
            uint16_t mFormat;
        };
        static_assert(sizeof(FMicromapTriangle) == 8);
        const uint8_t EncodedOpacity = 0xff;
        const FMicromapTriangle Triangle = {
            0, 0,
            static_cast<uint16_t>(
                EArdaRHIOpacityMicromapFormat::TwoState)};

        const float Vertices[9] = {
            -1.f, -1.f, 0.f,
             0.f,  1.f, 0.f,
             1.f, -1.f, 0.f};
        FArdaRHIBufferDesc VertexDesc;
        VertexDesc.mByteSize = sizeof(Vertices);
        VertexDesc.mUsage = EArdaRHIBufferUsage::Vertex |
            EArdaRHIBufferUsage::AccelStructBuildInput;
        VertexDesc.mCpuAccess = EArdaRHICpuAccess::Write;
        VertexDesc.mInitialState =
            EArdaRHIResourceState::AccelStructBuildInput;
        VertexDesc.mDebugName = "Opacity-micromap BLAS vertices";
        auto VertexBuffer = Device->CreateBuffer(VertexDesc);
        ASSERT_TRUE(VertexBuffer)
            << VertexBuffer.mStatus.mMessage.c_str();
        FArdaRHIRayTracingGeometryDesc Geometry;
        Geometry.mType = EArdaRHIRayTracingGeometryType::Triangles;
        Geometry.mVertexOrAABBBuffer = VertexBuffer.mValue;
        Geometry.mVertexFormat = EArdaRHIFormat::RGB32Float;
        Geometry.mVertexOrAABBCount = 3;
        Geometry.mStride = sizeof(float) * 3;
        Geometry.mOpacityMicromap = Micromap.mValue;
        Geometry.mOpacityMicromapUsageCounts = Desc.mCounts;
        FArdaRHIAccelStructDesc BlasDesc;
        BlasDesc.mBottomLevelGeometries.push_back(Geometry);
        BlasDesc.mBuildFlags =
            EArdaRHIAccelStructBuildFlags::PreferFastTrace;
        BlasDesc.mDebugName = "Opacity-micromap BLAS";
        auto Blas = Device->CreateAccelStruct(BlasDesc);
        ASSERT_TRUE(Blas) << Blas.mStatus.mMessage.c_str();

        auto Commands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ASSERT_TRUE(Commands.mValue->WriteBuffer(
            *Input.mValue, &EncodedOpacity, sizeof(EncodedOpacity)));
        ASSERT_TRUE(Commands.mValue->WriteBuffer(
            *Triangles.mValue, &Triangle, sizeof(Triangle)));
        ASSERT_TRUE(Commands.mValue->WriteBuffer(
            *VertexBuffer.mValue, Vertices, sizeof(Vertices)));
        auto Before = Commands.mValue->QueryOpacityMicromapState(
            *Micromap.mValue);
        ASSERT_TRUE(Before) << Before.mStatus.mMessage.c_str();
        EXPECT_TRUE(Before.mValue.IsConsistent());
        EXPECT_EQ(Before.mValue.mFacadeState,
            EArdaRHIResourceState::OpacityMicromapWrite);
        ASSERT_TRUE(Commands.mValue->BuildOpacityMicromap(
            *Micromap.mValue));
        const auto Built = Commands.mValue->QueryOpacityMicromapState(
            *Micromap.mValue);
        ASSERT_TRUE(Built) << Built.mStatus.mMessage.c_str();
        EXPECT_TRUE(Built.mValue.IsConsistent());
        EXPECT_TRUE(Built.mValue.mNative.mbNativeCompatible);
        EXPECT_EQ(Built.mValue.mNative.mNativeType,
            EArdaRHINativeResourceType::VulkanOpacityMicromap);
        EXPECT_EQ(Built.mValue.mFacadeState,
            EArdaRHIResourceState::OpacityMicromapBuildInput);
        ASSERT_TRUE(Commands.mValue->BuildBottomLevelAccelStruct(
            *Blas.mValue, {Geometry}, BlasDesc.mBuildFlags));
        const auto BlasState = Commands.mValue->QueryAccelStructState(
            *Blas.mValue);
        ASSERT_TRUE(BlasState) << BlasState.mStatus.mMessage.c_str();
        EXPECT_TRUE(BlasState.mValue.IsConsistent());
        EXPECT_EQ(BlasState.mValue.mFacadeState,
            EArdaRHIResourceState::AccelStructRead);
        ASSERT_TRUE(Commands.mValue->Close());
        const auto Submission = Device->ExecuteCommandList(Commands.mValue);
        ASSERT_TRUE(Submission) << Submission.mStatus.mMessage.c_str();
        ASSERT_TRUE(Device->WaitForIdle());
        EXPECT_EQ(Micromap.mValue->GetBuildState(),
            EArdaRHIAccelStructBuildState::Built);
        EXPECT_EQ(Blas.mValue->GetBuildState(),
            EArdaRHIAccelStructBuildState::Built);

        const auto CompactedSize =
            Device->GetOpacityMicromapCompactedSize(Micromap.mValue);
        ASSERT_TRUE(CompactedSize)
            << CompactedSize.mStatus.mMessage.c_str();
        ASSERT_GT(CompactedSize.mValue, 0u);
        FArdaRHIOpacityMicromapDesc CompactDesc = Desc;
        CompactDesc.mResultSizeOverride = CompactedSize.mValue;
        CompactDesc.mDebugName = "Compacted Vulkan opacity micromap";
        auto CompactMicromap = Device->CreateOpacityMicromap(CompactDesc);
        ASSERT_TRUE(CompactMicromap)
            << CompactMicromap.mStatus.mMessage.c_str();
        EXPECT_NE(CompactMicromap.mValue->GetPhysicalIdentity(), nullptr);

        FArdaRHIRayTracingGeometryDesc CompactGeometry = Geometry;
        CompactGeometry.mOpacityMicromap = CompactMicromap.mValue;
        FArdaRHIAccelStructDesc CompactBlasDesc = BlasDesc;
        CompactBlasDesc.mBottomLevelGeometries = {CompactGeometry};
        CompactBlasDesc.mDebugName = "Compacted-opacity-micromap BLAS";
        auto CompactBlas = Device->CreateAccelStruct(CompactBlasDesc);
        ASSERT_TRUE(CompactBlas)
            << CompactBlas.mStatus.mMessage.c_str();

        FArdaRHIAccelStructDesc TlasDesc;
        TlasDesc.mbTopLevel = true;
        TlasDesc.mTopLevelMaxInstances = 1;
        TlasDesc.mBuildFlags =
            EArdaRHIAccelStructBuildFlags::PreferFastTrace;
        TlasDesc.mDebugName = "Compacted-opacity-micromap TLAS";
        auto Tlas = Device->CreateAccelStruct(TlasDesc);
        ASSERT_TRUE(Tlas) << Tlas.mStatus.mMessage.c_str();

        auto RayGeneration = CreateExtendedShader(
            *Device, EArdaBackendType::Vulkan,
            "ArdaRayTracingTest", "KnownSceneRayGen",
            EArdaRHIShaderStage::RayGeneration);
        auto Miss = CreateExtendedShader(
            *Device, EArdaBackendType::Vulkan,
            "ArdaRayTracingTest", "KnownMiss",
            EArdaRHIShaderStage::Miss);
        auto ClosestHit = CreateExtendedShader(
            *Device, EArdaBackendType::Vulkan,
            "ArdaRayTracingTest", "KnownClosestHit",
            EArdaRHIShaderStage::ClosestHit);
        ASSERT_TRUE(RayGeneration);
        ASSERT_TRUE(Miss);
        ASSERT_TRUE(ClosestHit);
        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::AllRayTracing;
        LayoutDesc.mItems.push_back(
            {0, 1, EArdaRHIBindingType::RayTracingAccelStruct});
        LayoutDesc.mItems.push_back(
            {0, 1, EArdaRHIBindingType::StructuredBufferUAV});
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(Layout) << Layout.mStatus.mMessage.c_str();
        FArdaRHIRayTracingPipelineDesc PipelineDesc;
        PipelineDesc.mShaders.push_back(
            {"KnownSceneRayGen", RayGeneration.mValue, {}});
        PipelineDesc.mShaders.push_back(
            {"KnownMiss", Miss.mValue, {}});
        FArdaRHIRayTracingHitGroupDesc HitGroup;
        HitGroup.mExportName = "KnownHitGroup";
        HitGroup.mClosestHitShader = ClosestHit.mValue;
        PipelineDesc.mHitGroups.push_back(HitGroup);
        PipelineDesc.mGlobalBindingLayouts.push_back(Layout.mValue);
        PipelineDesc.mMaxPayloadSize = sizeof(uint32_t);
        PipelineDesc.mMaxRecursionDepth = 1;
        PipelineDesc.mbAllowOpacityMicromaps = true;
        PipelineDesc.mDebugName = "Opacity-micromap traversal pipeline";
        auto Pipeline = Device->CreateRayTracingPipeline(PipelineDesc);
        ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();
        FArdaRHIBufferDesc OutputDesc;
        OutputDesc.mByteSize = sizeof(uint32_t) * 2u;
        OutputDesc.mStructureStride = sizeof(uint32_t);
        OutputDesc.mUsage = EArdaRHIBufferUsage::Structured |
            EArdaRHIBufferUsage::UnorderedAccess;
        OutputDesc.mDebugName = "Opacity-micromap traversal output";
        auto Output = Device->CreateBuffer(OutputDesc);
        ASSERT_TRUE(Output) << Output.mStatus.mMessage.c_str();
        FArdaRHIBindingSetDesc SetDesc;
        SetDesc.mLayout = Layout.mValue;
        FArdaRHIBindingItem SceneItem;
        SceneItem.mType = EArdaRHIBindingType::RayTracingAccelStruct;
        SceneItem.mResource =
            TArdaRHIRef<IArdaRHIResource>(Tlas.mValue.Get());
        SetDesc.mItems.push_back(SceneItem);
        FArdaRHIBindingItem OutputItem;
        OutputItem.mType = EArdaRHIBindingType::StructuredBufferUAV;
        OutputItem.mResource =
            TArdaRHIRef<IArdaRHIResource>(Output.mValue.Get());
        SetDesc.mItems.push_back(OutputItem);
        auto BindingSet = Device->CreateBindingSet(SetDesc);
        ASSERT_TRUE(BindingSet)
            << BindingSet.mStatus.mMessage.c_str();
        FArdaRHIShaderTableDesc TableDesc;
        TableDesc.mMaxEntries = 3;
        TableDesc.mDebugName = "Opacity-micromap traversal SBT";
        auto ShaderTable = Device->CreateShaderTable(
            Pipeline.mValue, TableDesc);
        ASSERT_TRUE(ShaderTable)
            << ShaderTable.mStatus.mMessage.c_str();
        ASSERT_TRUE(Device->SetShaderTableRayGeneration(
            ShaderTable.mValue, "KnownSceneRayGen"));
        ASSERT_TRUE(Device->AddShaderTableMiss(
            ShaderTable.mValue, "KnownMiss"));
        ASSERT_TRUE(Device->AddShaderTableHitGroup(
            ShaderTable.mValue, "KnownHitGroup"));

        auto CompactCommands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(CompactCommands);
        ASSERT_TRUE(CompactCommands.mValue->Open());
        ASSERT_TRUE(CompactCommands.mValue->CompactOpacityMicromap(
            *CompactMicromap.mValue, *Micromap.mValue));
        const auto CompactState =
            CompactCommands.mValue->QueryOpacityMicromapState(
                *CompactMicromap.mValue);
        ASSERT_TRUE(CompactState)
            << CompactState.mStatus.mMessage.c_str();
        EXPECT_TRUE(CompactState.mValue.IsConsistent());
        EXPECT_TRUE(CompactState.mValue.mNative.mbNativeCompatible);
        EXPECT_EQ(CompactState.mValue.mFacadeState,
            EArdaRHIResourceState::OpacityMicromapBuildInput);
        ASSERT_TRUE(CompactCommands.mValue->BuildBottomLevelAccelStruct(
            *CompactBlas.mValue, {CompactGeometry},
            CompactBlasDesc.mBuildFlags));
        FArdaRHIRayTracingInstanceDesc Instance;
        Instance.mBottomLevelAccelStruct = CompactBlas.mValue;
        ASSERT_TRUE(CompactCommands.mValue->BuildTopLevelAccelStruct(
            *Tlas.mValue, {Instance}, TlasDesc.mBuildFlags));
        const auto TlasState =
            CompactCommands.mValue->QueryAccelStructState(*Tlas.mValue);
        ASSERT_TRUE(TlasState) << TlasState.mStatus.mMessage.c_str();
        EXPECT_TRUE(TlasState.mValue.IsConsistent());
        EXPECT_EQ(TlasState.mValue.mFacadeState,
            EArdaRHIResourceState::AccelStructRead);
        ASSERT_TRUE(CompactCommands.mValue->SetBufferState(
            *Output.mValue,
            EArdaRHIResourceState::UnorderedAccess));
        FArdaRHIRayTracingState RayState;
        RayState.mShaderTable = ShaderTable.mValue;
        RayState.mBindings.push_back(BindingSet.mValue);
        ASSERT_TRUE(CompactCommands.mValue->SetRayTracingState(RayState));
        ASSERT_TRUE(CompactCommands.mValue->DispatchRays(2, 1, 1));
        eastl::vector<uint8_t> TraversalReadback;
        ASSERT_TRUE(CompactCommands.mValue->CopyBufferDeviceToHost(
            *Output.mValue,
            TraversalReadback,
            0,
            OutputDesc.mByteSize));
        ExpectBufferState(
            *CompactCommands.mValue,
            *Output.mValue,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(CompactCommands.mValue->Close());
        const auto CompactSubmission =
            Device->ExecuteCommandList(CompactCommands.mValue);
        ASSERT_TRUE(CompactSubmission)
            << CompactSubmission.mStatus.mMessage.c_str();
        ASSERT_TRUE(Device->WaitForIdle());
        EXPECT_TRUE(CompactMicromap.mValue->IsCompacted());
        EXPECT_EQ(CompactMicromap.mValue->GetBuildState(),
            EArdaRHIAccelStructBuildState::Compacted);
        EXPECT_EQ(CompactBlas.mValue->GetBuildState(),
            EArdaRHIAccelStructBuildState::Built);
        ASSERT_EQ(TraversalReadback.size(), OutputDesc.mByteSize);
        uint32_t TraversalValues[2]{};
        std::memcpy(
            TraversalValues,
            TraversalReadback.data(),
            sizeof(TraversalValues));
        EXPECT_EQ(TraversalValues[0], 0xC105E57u);
        EXPECT_EQ(TraversalValues[1], 0xB055u);
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyExpandedDescriptorsAndResourceCollections(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;
        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = BackendName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        auto Device = GetDevice();
        ASSERT_TRUE(Device);
        const auto& Caps = Device->GetCapabilities();
        ASSERT_TRUE(Caps.mDescriptors.mbUnboundedArrays);
        ASSERT_TRUE(Caps.mDescriptors.mbUpdateAfterBind);
        ASSERT_TRUE(Caps.mDescriptors.mbVariableDescriptorCount);
        ASSERT_TRUE(Caps.mbResourceCollections);

        FArdaRHIBindlessLayoutDesc Bindless;
        Bindless.mVisibility = EArdaRHIShaderStage::All;
        Bindless.mbUnbounded = true;
        Bindless.mbUpdateAfterBind = true;
        Bindless.mbVariableDescriptorCount = true;
        Bindless.mLayoutType =
            EArdaRHIBindlessLayoutType::MutableSrvUavCbv;
        Bindless.mRegisterSpaces.push_back(
            {0, 1, EArdaRHIBindingType::RawBufferSRV});
        Bindless.mDebugName = "Expanded unbounded descriptors";
        auto Layout = Device->CreateBindlessLayout(Bindless);
        ASSERT_TRUE(Layout) << Layout.mStatus.mMessage.c_str();
        auto Table = Device->CreateDescriptorTable(Layout.mValue);
        ASSERT_TRUE(Table) << Table.mStatus.mMessage.c_str();
        EXPECT_GT(Table.mValue->GetCapacity(), 0u);
        ASSERT_TRUE(Device->ResizeDescriptorTable(Table.mValue, 8));
        EXPECT_EQ(Table.mValue->GetCapacity(), 8u);

        FArdaRHIBufferDesc BufferDesc;
        BufferDesc.mByteSize = 256;
        BufferDesc.mUsage = EArdaRHIBufferUsage::Raw |
            EArdaRHIBufferUsage::ShaderResource;
        BufferDesc.mDebugName = "Collection buffer A";
        auto BufferA = Device->CreateBuffer(BufferDesc);
        BufferDesc.mDebugName = "Collection buffer B";
        auto BufferB = Device->CreateBuffer(BufferDesc);
        ASSERT_TRUE(BufferA);
        ASSERT_TRUE(BufferB);
        FArdaRHIBindingItem Descriptor;
        Descriptor.mType = EArdaRHIBindingType::RawBufferSRV;
        Descriptor.mArrayElement = 7;
        Descriptor.mResource = FArdaRHIResourceRef(BufferA.mValue.Get());
        ASSERT_TRUE(Device->WriteDescriptorTable(Table.mValue, Descriptor));

        FArdaRHIResourceCollectionItem ItemA;
        ItemA.mType = EArdaRHIResourceCollectionItemType::Buffer;
        ItemA.mBuffer = BufferA.mValue;
        FArdaRHIResourceCollectionItem ItemB = ItemA;
        ItemB.mBuffer = BufferB.mValue;
        FArdaRHIResourceCollectionDesc CollectionDesc;
        CollectionDesc.mItems = {ItemA, ItemB};
        CollectionDesc.mbMutable = true;
        CollectionDesc.mbDirectlyIndexed =
            Caps.mDescriptors.mbDirectResourceHeapIndexing;
        CollectionDesc.mDebugName = "Mutable resource collection";
        auto Collection = Device->CreateResourceCollection(CollectionDesc);
        ASSERT_TRUE(Collection) << Collection.mStatus.mMessage.c_str();
        EXPECT_EQ(Collection.mValue->GetDesc().mItems.size(), 2u);
        if (CollectionDesc.mbDirectlyIndexed)
            EXPECT_NE(Collection.mValue->GetFirstDescriptorIndexInHeap(),
                0xffffffffu);
        FArdaRHIResourceCollectionItem Replacement = ItemA;
        Replacement.mBuffer = BufferB.mValue;
        ASSERT_TRUE(Device->UpdateResourceCollection(
            Collection.mValue, 0, Replacement));
        EXPECT_EQ(Collection.mValue->GetDesc().mItems[0].mBuffer,
            BufferB.mValue);
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifySparseResidencyAndStreamingBudget(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;
        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule(BackendName);
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = BackendName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        auto Device = GetDevice();
        ASSERT_TRUE(Device);
        const auto& Residency = Device->GetCapabilities().mResidency;
        if (!Residency.mbSparseBinding ||
            !Residency.mbReservedBuffers ||
            !Residency.mbReservedTexture2D)
            GTEST_SKIP() << "Native sparse residency is unavailable.";
        ASSERT_GT(Residency.mTileSizeInBytes, 0u);

        FArdaRHIBufferDesc BufferDesc;
        BufferDesc.mByteSize = Residency.mTileSizeInBytes * 2;
        BufferDesc.mUsage = EArdaRHIBufferUsage::Raw |
            EArdaRHIBufferUsage::ShaderResource;
        BufferDesc.mbTiled = true;
        BufferDesc.mDebugName = "Sparse parity buffer";
        auto Buffer = Device->CreateBuffer(BufferDesc);
        ASSERT_TRUE(Buffer) << Buffer.mStatus.mMessage.c_str();
        auto BufferMemory = Device->GetBufferMemoryRequirements(Buffer.mValue);
        ASSERT_TRUE(BufferMemory);
        FArdaRHIHeapDesc HeapDesc;
        HeapDesc.mCapacity = eastl::max<uint64_t>(
            BufferMemory.mValue.mAlignment,
            Residency.mTileSizeInBytes * 2);
        HeapDesc.mMemoryTypeBits = BufferMemory.mValue.mMemoryTypeBits;
        HeapDesc.mDebugName = "Sparse parity heap";
        auto Heap = Device->CreateHeap(HeapDesc);
        ASSERT_TRUE(Heap) << Heap.mStatus.mMessage.c_str();
        const uint64_t BufferTileBytes = BufferMemory.mValue.mAlignment;
        FArdaRHIBufferTileMapping BufferMapping;
        BufferMapping.mByteSize = BufferTileBytes;
        BufferMapping.mHeap = Heap.mValue;
        ASSERT_TRUE(Device->UpdateBufferTileMappings(
            Buffer.mValue, {BufferMapping},
            EArdaRHIQueueType::Graphics));
        BufferMapping.mbCommit = false;
        BufferMapping.mHeap = {};
        ASSERT_TRUE(Device->UpdateBufferTileMappings(
            Buffer.mValue, {BufferMapping},
            EArdaRHIQueueType::Graphics));
        ASSERT_TRUE(Device->CommitReservedResource(
            FArdaRHIResourceRef(Buffer.mValue.Get()),
            BufferTileBytes,
            EArdaRHIQueueType::Graphics));
        const uint32_t SparseExpected = 0x51A25EEDu;
        auto SparseCommands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(SparseCommands);
        ASSERT_TRUE(SparseCommands.mValue->Open());
        ASSERT_TRUE(SparseCommands.mValue->WriteBuffer(
            *Buffer.mValue,
            &SparseExpected,
            sizeof(SparseExpected),
            0));
        eastl::vector<uint8_t> SparseReadback;
        ASSERT_TRUE(SparseCommands.mValue->CopyBufferDeviceToHost(
            *Buffer.mValue,
            SparseReadback,
            0,
            sizeof(SparseExpected)));
        ExpectBufferState(
            *SparseCommands.mValue,
            *Buffer.mValue,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(SparseCommands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(SparseCommands.mValue));
        ASSERT_EQ(SparseReadback.size(), sizeof(SparseExpected));
        uint32_t SparseActual = 0;
        std::memcpy(
            &SparseActual,
            SparseReadback.data(),
            sizeof(SparseActual));
        EXPECT_EQ(SparseActual, SparseExpected);
        ASSERT_TRUE(Device->CommitReservedResource(
            FArdaRHIResourceRef(Buffer.mValue.Get()), 0,
            EArdaRHIQueueType::Graphics));

        FArdaRHITextureDesc TextureDesc;
        TextureDesc.mWidth = 256;
        TextureDesc.mHeight = 256;
        TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
        TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource;
        TextureDesc.mbTiled = true;
        TextureDesc.mDebugName = "Sparse parity texture";
        auto Texture = Device->CreateTexture(TextureDesc);
        ASSERT_TRUE(Texture) << Texture.mStatus.mMessage.c_str();
        const auto Tiling = Device->GetTextureTiling(Texture.mValue);
        ASSERT_TRUE(Tiling) << Tiling.mStatus.mMessage.c_str();
        ASSERT_GT(Tiling.mValue.mTileCount, 0u);
        ASSERT_FALSE(Tiling.mValue.mSubresources.empty());
        FArdaRHITextureTileMapping TextureMapping;
        TextureMapping.mCoordinates.push_back({0, 0, 0, 0, 0});
        TextureMapping.mRegions.push_back({1, 1, 1, 1});
        TextureMapping.mByteOffsets.push_back(0);
        TextureMapping.mHeap = Heap.mValue;
        const auto TextureMapStatus = Device->UpdateTextureTileMappings(
            Texture.mValue, {TextureMapping},
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(TextureMapStatus) << TextureMapStatus.mMessage.c_str();
        TextureMapping.mHeap = {};
        ASSERT_TRUE(Device->UpdateTextureTileMappings(
            Texture.mValue, {TextureMapping},
            EArdaRHIQueueType::Graphics));

        if (Residency.mbReservedTexture3D)
        {
            FArdaRHITextureDesc VolumeDesc = TextureDesc;
            VolumeDesc.mDebugName = "Sparse parity 3D texture";
            VolumeDesc.mDimension = EArdaRHITextureDimension::Texture3D;
            VolumeDesc.mWidth = 64;
            VolumeDesc.mHeight = 64;
            VolumeDesc.mDepth = 16;
            auto Volume = Device->CreateTexture(VolumeDesc);
            ASSERT_TRUE(Volume) << Volume.mStatus.mMessage.c_str();
            const auto VolumeTiling = Device->GetTextureTiling(Volume.mValue);
            ASSERT_TRUE(VolumeTiling)
                << VolumeTiling.mStatus.mMessage.c_str();
            ASSERT_GT(VolumeTiling.mValue.mTileCount, 0u);
            FArdaRHITextureTileMapping VolumeMapping;
            VolumeMapping.mCoordinates.push_back({0, 0, 0, 0, 0});
            VolumeMapping.mRegions.push_back({1, 1, 1, 1});
            VolumeMapping.mByteOffsets.push_back(0);
            VolumeMapping.mHeap = Heap.mValue;
            ASSERT_TRUE(Device->UpdateTextureTileMappings(
                Volume.mValue, {VolumeMapping},
                EArdaRHIQueueType::Graphics));
            VolumeMapping.mHeap = {};
            ASSERT_TRUE(Device->UpdateTextureTileMappings(
                Volume.mValue, {VolumeMapping},
                EArdaRHIQueueType::Graphics));
        }

        if (Residency.mbAliasedMappings)
        {
            BufferDesc.mDebugName = "Sparse parity alias buffer";
            auto Alias = Device->CreateBuffer(BufferDesc);
            ASSERT_TRUE(Alias) << Alias.mStatus.mMessage.c_str();
            FArdaRHIBufferTileMapping AliasedMapping;
            AliasedMapping.mByteSize = BufferTileBytes;
            AliasedMapping.mHeap = Heap.mValue;
            ASSERT_TRUE(Device->UpdateBufferTileMappings(
                Buffer.mValue, {AliasedMapping},
                EArdaRHIQueueType::Graphics));
            ASSERT_TRUE(Device->UpdateBufferTileMappings(
                Alias.mValue, {AliasedMapping},
                EArdaRHIQueueType::Graphics));
            AliasedMapping.mbCommit = false;
            AliasedMapping.mHeap = {};
            ASSERT_TRUE(Device->UpdateBufferTileMappings(
                Alias.mValue, {AliasedMapping},
                EArdaRHIQueueType::Graphics));
            ASSERT_TRUE(Device->UpdateBufferTileMappings(
                Buffer.mValue, {AliasedMapping},
                EArdaRHIQueueType::Graphics));
        }

        if (Residency.mbStreamingBudget)
        {
            const auto Budget = Device->QueryStreamingBudget(true);
            ASSERT_TRUE(Budget) << Budget.mStatus.mMessage.c_str();
            EXPECT_GT(Budget.mValue.mBudgetBytes, 0u);
            EXPECT_TRUE(Budget.mValue.mbLocalMemory);
            if (Residency.mbBudgetReservation)
                ASSERT_TRUE(Device->SetStreamingBudgetReservation(
                    Budget.mValue.mCurrentReservationBytes, true));
        }
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyStreamingBudgetExecution(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = BackendName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();

        auto Device = GetDevice();
        ASSERT_TRUE(Device);
        const auto& Residency = Device->GetCapabilities().mResidency;
        ASSERT_TRUE(Residency.mbStreamingBudget);
        const auto Budget = Device->QueryStreamingBudget(true);
        ASSERT_TRUE(Budget) << Budget.mStatus.mMessage.c_str();
        EXPECT_GT(Budget.mValue.mBudgetBytes, 0u);
        EXPECT_TRUE(Budget.mValue.mbLocalMemory);
        if (Residency.mbBudgetReservation)
        {
            ASSERT_TRUE(Device->SetStreamingBudgetReservation(
                Budget.mValue.mCurrentReservationBytes, true));
        }
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyQueryExecution(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = BackendName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();

        auto Device = GetDevice();
        ASSERT_TRUE(Device);
        ASSERT_TRUE(Device->GetCapabilities().mbQueries);
        auto Event = Device->CreateEventQuery();
        auto Timer = Device->CreateTimerQuery();
        ASSERT_TRUE(Event) << Event.mStatus.mMessage.c_str();
        ASSERT_TRUE(Timer) << Timer.mStatus.mMessage.c_str();

        ASSERT_TRUE(Device->SignalEventQuery(
            Event.mValue, EArdaRHIQueueType::Graphics));
        ASSERT_TRUE(Device->WaitEventQuery(Event.mValue));
        const auto EventComplete = Device->PollEventQuery(Event.mValue);
        ASSERT_TRUE(EventComplete) << EventComplete.mStatus.mMessage.c_str();
        EXPECT_TRUE(EventComplete.mValue);
        ASSERT_TRUE(Device->ResetEventQuery(Event.mValue));

        auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands) << Commands.mStatus.mMessage.c_str();
        ASSERT_TRUE(Commands.mValue->Open());
        ASSERT_TRUE(Commands.mValue->BeginTimerQuery(*Timer.mValue));
        ASSERT_TRUE(Commands.mValue->EndTimerQuery(*Timer.mValue));
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        const auto TimerComplete = Device->PollTimerQuery(Timer.mValue);
        ASSERT_TRUE(TimerComplete) << TimerComplete.mStatus.mMessage.c_str();
        EXPECT_TRUE(TimerComplete.mValue);
        const auto Seconds = Device->GetTimerQuerySeconds(Timer.mValue);
        ASSERT_TRUE(Seconds) << Seconds.mStatus.mMessage.c_str();
        EXPECT_GE(Seconds.mValue, 0.0f);
        ASSERT_TRUE(Device->ResetTimerQuery(Timer.mValue));
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyShaderLibraryExecution(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = BackendName;
        Configuration.mBackend = Backend;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();

        auto Device = GetDevice();
        ASSERT_TRUE(Device);
        ASSERT_TRUE(Device->GetCapabilities().mbShaderLibraries);
        const eastl::string FileName = eastl::string("ArdaShaderStructTest") +
            GetShaderArtifactExtension(Backend);
        const std::vector<uint8_t> Bytecode =
            LoadExtendedShaderArtifact(FileName.c_str());
        ASSERT_FALSE(Bytecode.empty());
        auto Library = Device->CreateShaderLibrary(
            Bytecode.data(), Bytecode.size(), "Capability shader library");
        ASSERT_TRUE(Library) << Library.mStatus.mMessage.c_str();
        auto Shader = Device->GetShaderFromLibrary(
            Library.mValue, "ShaderStructTestCS",
            EArdaRHIShaderStage::Compute,
            "Capability shader-library compute shader");
        ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();

        FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
        LayoutDesc.mItems.push_back(
            {0, 1, EArdaRHIBindingType::StructuredBufferUAV});
        auto Layout = Device->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(Layout) << Layout.mStatus.mMessage.c_str();
        FArdaRHIComputePipelineDesc PipelineDesc;
        PipelineDesc.mComputeShader = Shader.mValue;
        PipelineDesc.mBindingLayouts.push_back(Layout.mValue);
        ASSERT_TRUE(Device->CreateComputePipeline(PipelineDesc));
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

#if defined(_WIN32)
    void VerifyD3D12DeferredSubmissionLifetime()
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FExtendedDiagnosticCallback Diagnostics;
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = "native-d3d12";
        Configuration.mBackend = EArdaBackendType::D3D12;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        if (!InitializeBackend())
            GTEST_SKIP() << GetBackendError().c_str();
        auto Device = GetDevice();
        ASSERT_TRUE(Device);

        FArdaRHIBufferDesc BufferDesc;
        BufferDesc.mByteSize = sizeof(uint32_t);
        BufferDesc.mUsage = EArdaRHIBufferUsage::Raw |
            EArdaRHIBufferUsage::ShaderResource;
        BufferDesc.mInitialState = EArdaRHIResourceState::Common;
        BufferDesc.mDebugName = "Deferred D3D12 submission lifetime";
        auto Buffer = Device->CreateBuffer(BufferDesc);
        ASSERT_TRUE(Buffer) << Buffer.mStatus.mMessage.c_str();

        constexpr uint32_t SubmissionCount = 256;
        for (uint32_t Value = 1; Value <= SubmissionCount; ++Value)
        {
            auto Commands = Device->CreateCommandList(
                EArdaRHIQueueType::Graphics);
            ASSERT_TRUE(Commands);
            ASSERT_TRUE(Commands.mValue->Open());
            ASSERT_TRUE(Commands.mValue->WriteBuffer(
                *Buffer.mValue, &Value, sizeof(Value), 0));
            ASSERT_TRUE(Commands.mValue->Close());
            const auto Submission =
                Device->ExecuteCommandList(Commands.mValue);
            ASSERT_TRUE(Submission)
                << Submission.mStatus.mMessage.c_str();
            Commands.mValue = nullptr;
            Device->RunGarbageCollection();
        }

        auto ReadbackCommands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(ReadbackCommands);
        ASSERT_TRUE(ReadbackCommands.mValue->Open());
        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(ReadbackCommands.mValue->CopyBufferDeviceToHost(
            *Buffer.mValue, Readback, 0, sizeof(uint32_t)));
        ExpectBufferState(
            *ReadbackCommands.mValue,
            *Buffer.mValue,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(ReadbackCommands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(ReadbackCommands.mValue));
        ASSERT_EQ(Readback.size(), sizeof(uint32_t));
        uint32_t Actual = 0;
        std::memcpy(&Actual, Readback.data(), sizeof(Actual));
        EXPECT_EQ(Actual, SubmissionCount);
        Device->RunGarbageCollection();
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

    void VerifyD3D12CustomPresentExecution()
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FWin32TestSurface Surface;
        ASSERT_TRUE(Surface.IsValid());

        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule("native-d3d12");
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = EArdaBackendType::D3D12;
        Configuration.mbEnableValidation = true;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));

        eastl::unique_ptr<IArdaSwapChain> SwapChain;
        const EArdaInitializeResult Result = InitializeBackendForPresentation(
            Surface, 16, 16, SwapChain);
        if (Result != EArdaInitializeResult::Success)
            GTEST_SKIP() << GetBackendError().c_str();
        ASSERT_TRUE(SwapChain);
        ASSERT_EQ(SwapChain->GetWidth(), 16u);
        ASSERT_EQ(SwapChain->GetHeight(), 16u);

        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        const FArdaRHIStatus Support = Device->QueryCustomPresentSupport();
        ASSERT_TRUE(Support) << Support.mMessage.c_str();

        auto Tracker = eastl::make_shared<FCustomPresentTracker>();
        SwapChain->SetCustomPresent(Tracker);
        ASSERT_EQ(SwapChain->GetCustomPresent().get(), Tracker.get());
        EXPECT_EQ(Tracker->mResizeCount, 1u);
        EXPECT_EQ(Tracker->mWidth, 16u);
        EXPECT_EQ(Tracker->mHeight, 16u);

        FArdaRHIFramebufferRef Framebuffer;
        ASSERT_TRUE(SwapChain->AcquireFrame(Framebuffer));
        ASSERT_TRUE(Framebuffer);
        SwapChain->PrepareSubmit();
        ASSERT_TRUE(SwapChain->Present()) << SwapChain->GetError().c_str();
        EXPECT_EQ(Tracker->mPresentCount, 1u);
        EXPECT_EQ(Tracker->mPostPresentCount, 1u);
        EXPECT_TRUE(static_cast<bool>(Tracker->mBackBuffer));

        Framebuffer = nullptr;
        ASSERT_TRUE(SwapChain->Resize(24, 20))
            << SwapChain->GetError().c_str();
        EXPECT_EQ(Tracker->mResizeCount, 2u);
        EXPECT_EQ(Tracker->mWidth, 24u);
        EXPECT_EQ(Tracker->mHeight, 20u);
        ASSERT_TRUE(SwapChain->AcquireFrame(Framebuffer));
        ASSERT_TRUE(SwapChain->Present()) << SwapChain->GetError().c_str();
        EXPECT_EQ(Tracker->mPresentCount, 2u);
        EXPECT_EQ(Tracker->mPostPresentCount, 2u);

        SwapChain->SetCustomPresent({});
        EXPECT_FALSE(SwapChain->GetCustomPresent());
        SwapChain->WaitForIdle();
        SwapChain.reset();
        ShutdownBackend();
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }

#if defined(ARDA_TEST_NATIVE_VULKAN)
    void VerifyVulkanCustomPresentExecution()
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FExtendedBackendCleanup Cleanup;
        FWin32TestSurface Surface;
        ASSERT_TRUE(Surface.IsValid());

        FExtendedDiagnosticCallback Diagnostics;
        IArdaBackendModule* Module = FindBackendModule("native-vulkan");
        ASSERT_NE(Module, nullptr);
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module->GetDescriptor().mName;
        Configuration.mBackend = EArdaBackendType::Vulkan;
        Configuration.mbEnableValidation = false;
        Configuration.mMessageCallback = &Diagnostics;
        Configuration.mShaderCompilationMode =
            EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));

        eastl::unique_ptr<IArdaSwapChain> SwapChain;
        const EArdaInitializeResult Result = InitializeBackendForPresentation(
            Surface, 64, 64, SwapChain);
        if (Result != EArdaInitializeResult::Success)
            GTEST_SKIP() << GetBackendError().c_str();
        ASSERT_TRUE(SwapChain);

        FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        const FArdaRHIStatus Support = Device->QueryCustomPresentSupport();
        ASSERT_TRUE(Support) << Support.mMessage.c_str();

        auto Tracker = eastl::make_shared<FCustomPresentTracker>();
        SwapChain->SetCustomPresent(Tracker);
        ASSERT_EQ(SwapChain->GetCustomPresent().get(), Tracker.get());
        EXPECT_EQ(Tracker->mResizeCount, 1u);
        EXPECT_EQ(Tracker->mWidth, SwapChain->GetWidth());
        EXPECT_EQ(Tracker->mHeight, SwapChain->GetHeight());

        FArdaRHIFramebufferRef Framebuffer;
        ASSERT_TRUE(SwapChain->AcquireFrame(Framebuffer))
            << SwapChain->GetError().c_str();
        ASSERT_TRUE(Framebuffer);
        SwapChain->PrepareSubmit();
        ASSERT_TRUE(SwapChain->Present()) << SwapChain->GetError().c_str();
        EXPECT_EQ(Tracker->mPresentCount, 1u);
        EXPECT_EQ(Tracker->mPostPresentCount, 1u);
        EXPECT_TRUE(static_cast<bool>(Tracker->mBackBuffer));

        Framebuffer = nullptr;
        ASSERT_TRUE(SwapChain->Resize(72, 68))
            << SwapChain->GetError().c_str();
        EXPECT_EQ(Tracker->mResizeCount, 2u);
        EXPECT_EQ(Tracker->mWidth, SwapChain->GetWidth());
        EXPECT_EQ(Tracker->mHeight, SwapChain->GetHeight());

        SwapChain->SetCustomPresent({});
        EXPECT_FALSE(SwapChain->GetCustomPresent());
        SwapChain->WaitForIdle();
        SwapChain.reset();
        ShutdownBackend();
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }
#endif
#endif

    enum class ECapabilityProbe : uint8_t
    {
        Contract,
        ExtendedCommands,
        Resolve,
        HeapAliasing,
        Bindless,
        DirectResourceHeap,
        DirectResourceAndSamplerHeaps,
        ExpandedDescriptors,
        QueueBreadth,
        SparseResidency,
        StreamingBudget,
        ShaderBundle,
        WorkGraph,
        MeshShader,
        RayTracingPipeline,
        AccelerationStructure,
        RayTracingScene,
        OpacityMicromap,
        SamplerFeedback,
        CustomPresent,
        Queries,
        ShaderLibrary
    };

    using FCapabilityPredicate = bool (*)(
        const arda::rhi::FArdaRHICapabilities&);

    struct FCapabilityDefinition
    {
        const char* mName = nullptr;
        FCapabilityPredicate mIsAdvertised = nullptr;
        ECapabilityProbe mProbe = ECapabilityProbe::Contract;
    };

    struct FCapabilityConformanceCase
    {
        arda::backend::EArdaBackendType mBackend =
            arda::backend::EArdaBackendType::D3D12;
        const char* mBackendName = nullptr;
        const char* mBackendLabel = nullptr;
        FCapabilityDefinition mCapability;
    };

    void VerifyCapabilityInvariants(
        const arda::rhi::FArdaRHICapabilities& Caps)
    {
        using namespace arda::rhi;
        const auto& Ray = Caps.mRayTracing;
        if (Ray.mbHardwareAccelerated)
            EXPECT_TRUE(Ray.mbInfrastructure);
        if (Ray.mbPipelineShaders)
        {
            EXPECT_TRUE(Ray.mbInfrastructure);
            EXPECT_TRUE(Ray.mbHardwareAccelerated);
        }
        if (Ray.mbInlineRayQueries)
        {
            EXPECT_TRUE(Ray.mbHardwareAccelerated);
            EXPECT_TRUE(Ray.mbAccelerationStructures);
        }
        if (Ray.mbBottomLevel || Ray.mbTopLevel || Ray.mbBuildUpdate ||
            Ray.mbCompaction)
            EXPECT_TRUE(Ray.mbAccelerationStructures);
        if (Ray.mbIndirectDispatch || Ray.mbLocalShaderTableArguments ||
            Ray.mbPersistentShaderTables)
            EXPECT_TRUE(Ray.mbPipelineShaders);
        if (Ray.mbIndirectTopLevelBuild)
            EXPECT_TRUE(Ray.mbTopLevel);
        if (Ray.mbOpacityMicromaps)
            EXPECT_TRUE(Ray.mbAccelerationStructures);

        const auto& Descriptors = Caps.mDescriptors;
        if (Descriptors.mbRuntimeDescriptorArrays ||
            Descriptors.mbUnboundedArrays || Descriptors.mbPartiallyBound ||
            Descriptors.mbUpdateAfterBind ||
            Descriptors.mbUpdateUnusedWhilePending ||
            Descriptors.mbVariableDescriptorCount ||
            Descriptors.mbDirectResourceHeapIndexing ||
            Descriptors.mbDirectSamplerHeapIndexing ||
            Descriptors.mbDescriptorBuffer || Descriptors.mbDescriptorHeap)
            EXPECT_TRUE(Descriptors.mbBindless);
        if (Descriptors.mbDirectSamplerHeapIndexing)
            EXPECT_TRUE(Descriptors.mbDirectResourceHeapIndexing);

        const auto& Queues = Caps.mQueues;
        if (Queues.mbDedicatedComputeFamily)
            EXPECT_TRUE(Queues.mbCompute);
        if (Queues.mbDedicatedCopyFamily)
            EXPECT_TRUE(Queues.mbCopy);
        if (Queues.mbSparseBindingQueue)
            EXPECT_TRUE(Queues.mbGraphics || Queues.mbCompute || Queues.mbCopy);

        const auto& Residency = Caps.mResidency;
        if (Residency.mbReservedBuffers || Residency.mbReservedTexture2D ||
            Residency.mbReservedTexture3D || Residency.mbAliasedMappings)
            EXPECT_TRUE(Residency.mbSparseBinding);
        if (Residency.mbBudgetReservation)
            EXPECT_TRUE(Residency.mbStreamingBudget);

        const auto& MachineLearning = Caps.mMachineLearning;
        if (MachineLearning.mbSubgroupOperations)
        {
            EXPECT_GT(MachineLearning.mSubgroupMinSize, 0u);
            EXPECT_GE(MachineLearning.mSubgroupMaxSize,
                MachineLearning.mSubgroupMinSize);
        }
    }

    void RunCapabilityProbe(const FCapabilityConformanceCase& TestCase)
    {
        using namespace arda::backend;
        switch (TestCase.mCapability.mProbe)
        {
        case ECapabilityProbe::Contract:
            return;
        case ECapabilityProbe::ExtendedCommands:
            VerifyExtendedCommands(TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::Resolve:
            VerifyResolveAndPlaneTracking(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::HeapAliasing:
            VerifyExplicitHeapAliasing(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::Bindless:
            VerifyBindlessDescriptorTable(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::DirectResourceHeap:
            VerifyBindlessDescriptorTable(
                TestCase.mBackend, TestCase.mBackendName, true);
            return;
        case ECapabilityProbe::DirectResourceAndSamplerHeaps:
            VerifyDirectResourceAndSamplerHeapIndexing(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::ExpandedDescriptors:
            VerifyExpandedDescriptorsAndResourceCollections(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::QueueBreadth:
            VerifyQueueBreadth(TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::SparseResidency:
            VerifySparseResidencyAndStreamingBudget(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::StreamingBudget:
            VerifyStreamingBudgetExecution(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::ShaderBundle:
            VerifyShaderBundleExecution(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::WorkGraph:
            ASSERT_EQ(TestCase.mBackend, EArdaBackendType::D3D12)
                << "A backend advertised work graphs without a native "
                   "conformance workload.";
            VerifyD3D12WorkGraphExecution();
            return;
        case ECapabilityProbe::MeshShader:
            VerifyMeshPipelineCapabilityAndExecution(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::RayTracingPipeline:
            VerifyRayTracingPipelineCapabilityAndExecution(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::AccelerationStructure:
            VerifyAccelerationStructureLifecycleAndStateParity(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::RayTracingScene:
            VerifyRayTracingSceneHitGroupsLocalArgumentsAndIndirect(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::OpacityMicromap:
#if defined(ARDA_TEST_NATIVE_VULKAN)
            ASSERT_EQ(TestCase.mBackend, EArdaBackendType::Vulkan)
                << "A non-Vulkan provider advertised opacity micromaps "
                   "without a conformance implementation.";
            VerifyVulkanOpacityMicromapLifecycleAndStateParity();
#else
            FAIL() << "Opacity micromaps were advertised without the Vulkan "
                      "conformance implementation.";
#endif
            return;
        case ECapabilityProbe::SamplerFeedback:
            ASSERT_EQ(TestCase.mBackend, EArdaBackendType::D3D12)
                << "A non-D3D12 provider advertised sampler feedback without "
                   "a conformance implementation.";
            VerifySamplerFeedbackStateParity();
            return;
        case ECapabilityProbe::CustomPresent:
#if defined(_WIN32)
            if (TestCase.mBackend == EArdaBackendType::D3D12)
            {
                VerifyD3D12CustomPresentExecution();
                return;
            }
#if defined(ARDA_TEST_NATIVE_VULKAN)
            VerifyVulkanCustomPresentExecution();
            return;
#endif
#endif
            FAIL() << "Custom present was advertised without a platform "
                      "conformance implementation.";
            return;
        case ECapabilityProbe::Queries:
            VerifyQueryExecution(TestCase.mBackend, TestCase.mBackendName);
            return;
        case ECapabilityProbe::ShaderLibrary:
            VerifyShaderLibraryExecution(
                TestCase.mBackend, TestCase.mBackendName);
            return;
        }
        FAIL() << "Unknown RHI capability conformance probe.";
    }

#define ARDA_CAPABILITY(Name, Expression, Probe) \
    { Name, +[](const arda::rhi::FArdaRHICapabilities& C) \
        { return static_cast<bool>(Expression); }, ECapabilityProbe::Probe }

    const std::vector<FCapabilityConformanceCase>&
    GetCapabilityConformanceCases()
    {
        static const FCapabilityDefinition Definitions[] = {
            ARDA_CAPABILITY("RayTracingInfrastructure",
                C.mRayTracing.mbInfrastructure, RayTracingPipeline),
            ARDA_CAPABILITY("HardwareRayTracing",
                C.mRayTracing.mbHardwareAccelerated, AccelerationStructure),
            ARDA_CAPABILITY("RayTracingPipelineShaders",
                C.mRayTracing.mbPipelineShaders, RayTracingPipeline),
            ARDA_CAPABILITY("InlineRayQueries",
                C.mRayTracing.mbInlineRayQueries, AccelerationStructure),
            ARDA_CAPABILITY("AccelerationStructures",
                C.mRayTracing.mbAccelerationStructures, AccelerationStructure),
            ARDA_CAPABILITY("BottomLevelAccelerationStructures",
                C.mRayTracing.mbBottomLevel, AccelerationStructure),
            ARDA_CAPABILITY("TopLevelAccelerationStructures",
                C.mRayTracing.mbTopLevel, AccelerationStructure),
            ARDA_CAPABILITY("AccelerationStructureBuildUpdate",
                C.mRayTracing.mbBuildUpdate, AccelerationStructure),
            ARDA_CAPABILITY("AccelerationStructureCompaction",
                C.mRayTracing.mbCompaction, AccelerationStructure),
            ARDA_CAPABILITY("IndirectRayDispatch",
                C.mRayTracing.mbIndirectDispatch, RayTracingScene),
            ARDA_CAPABILITY("IndirectTopLevelBuild",
                C.mRayTracing.mbIndirectTopLevelBuild, AccelerationStructure),
            ARDA_CAPABILITY("LocalShaderTableArguments",
                C.mRayTracing.mbLocalShaderTableArguments, RayTracingScene),
            ARDA_CAPABILITY("PersistentShaderTables",
                C.mRayTracing.mbPersistentShaderTables, RayTracingScene),
            ARDA_CAPABILITY("OpacityMicromaps",
                C.mRayTracing.mbOpacityMicromaps, OpacityMicromap),
            ARDA_CAPABILITY("RayTracingTier",
                C.mRayTracing.GetTier() !=
                    arda::rhi::EArdaRHIRayTracingTier::None,
                AccelerationStructure),
            ARDA_CAPABILITY("RayShaderIdentifierSize",
                C.mRayTracing.mShaderIdentifierSize > 0, RayTracingPipeline),
            ARDA_CAPABILITY("RayShaderRecordAlignment",
                C.mRayTracing.mShaderRecordAlignment > 0, RayTracingPipeline),
            ARDA_CAPABILITY("RayShaderTableAlignment",
                C.mRayTracing.mShaderTableAlignment > 0, RayTracingPipeline),
            ARDA_CAPABILITY("AccelerationStructureAlignment",
                C.mRayTracing.mAccelerationStructureAlignment > 0,
                AccelerationStructure),
            ARDA_CAPABILITY("MaximumRayRecursionDepth",
                C.mRayTracing.mMaxRecursionDepth > 0, RayTracingPipeline),
            ARDA_CAPABILITY("MaximumRayPayloadSize",
                C.mRayTracing.mMaxRayPayloadSize > 0, RayTracingPipeline),
            ARDA_CAPABILITY("MaximumRayDispatchInvocations",
                C.mRayTracing.mMaxRayDispatchInvocations > 0,
                RayTracingPipeline),

            ARDA_CAPABILITY("BindlessDescriptors",
                C.mDescriptors.mbBindless, Bindless),
            ARDA_CAPABILITY("RuntimeDescriptorArrays",
                C.mDescriptors.mbRuntimeDescriptorArrays, Bindless),
            ARDA_CAPABILITY("UnboundedDescriptorArrays",
                C.mDescriptors.mbUnboundedArrays, ExpandedDescriptors),
            ARDA_CAPABILITY("PartiallyBoundDescriptors",
                C.mDescriptors.mbPartiallyBound, Bindless),
            ARDA_CAPABILITY("DescriptorUpdateAfterBind",
                C.mDescriptors.mbUpdateAfterBind, ExpandedDescriptors),
            ARDA_CAPABILITY("UpdateUnusedDescriptorsWhilePending",
                C.mDescriptors.mbUpdateUnusedWhilePending, Bindless),
            ARDA_CAPABILITY("VariableDescriptorCount",
                C.mDescriptors.mbVariableDescriptorCount,
                ExpandedDescriptors),
            ARDA_CAPABILITY("DirectResourceHeapIndexing",
                C.mDescriptors.mbDirectResourceHeapIndexing,
                DirectResourceHeap),
            ARDA_CAPABILITY("DirectSamplerHeapIndexing",
                C.mDescriptors.mbDirectSamplerHeapIndexing,
                DirectResourceAndSamplerHeaps),
            ARDA_CAPABILITY("DescriptorBuffer",
                C.mDescriptors.mbDescriptorBuffer, Bindless),
            ARDA_CAPABILITY("DescriptorHeap",
                C.mDescriptors.mbDescriptorHeap, Bindless),
            ARDA_CAPABILITY("MaximumResourceDescriptors",
                C.mDescriptors.mMaxResourceDescriptors > 0, Bindless),
            ARDA_CAPABILITY("MaximumSamplerDescriptors",
                C.mDescriptors.mMaxSamplerDescriptors > 0, Bindless),

            ARDA_CAPABILITY("GraphicsQueue", C.mQueues.mbGraphics,
                QueueBreadth),
            ARDA_CAPABILITY("ComputeQueue", C.mQueues.mbCompute,
                QueueBreadth),
            ARDA_CAPABILITY("CopyQueue", C.mQueues.mbCopy, QueueBreadth),
            ARDA_CAPABILITY("DedicatedComputeQueueFamily",
                C.mQueues.mbDedicatedComputeFamily, QueueBreadth),
            ARDA_CAPABILITY("DedicatedCopyQueueFamily",
                C.mQueues.mbDedicatedCopyFamily, QueueBreadth),
            ARDA_CAPABILITY("GpuQueueWaits", C.mQueues.mbGpuWaits,
                QueueBreadth),
            ARDA_CAPABILITY("TimelineSynchronization",
                C.mQueues.mbTimelineSynchronization, QueueBreadth),
            ARDA_CAPABILITY("QueueFamilyOwnershipTransfer",
                C.mQueues.mbQueueFamilyOwnershipTransfer, QueueBreadth),
            ARDA_CAPABILITY("SparseBindingQueue",
                C.mQueues.mbSparseBindingQueue, SparseResidency),
            ARDA_CAPABILITY("GraphicsQueueFamilyIndex",
                C.mQueues.mGraphicsFamily !=
                    arda::rhi::ArdaRHIInvalidQueueFamily, QueueBreadth),
            ARDA_CAPABILITY("ComputeQueueFamilyIndex",
                C.mQueues.mComputeFamily !=
                    arda::rhi::ArdaRHIInvalidQueueFamily, QueueBreadth),
            ARDA_CAPABILITY("CopyQueueFamilyIndex",
                C.mQueues.mCopyFamily !=
                    arda::rhi::ArdaRHIInvalidQueueFamily, QueueBreadth),

            ARDA_CAPABILITY("SparseBinding",
                C.mResidency.mbSparseBinding, SparseResidency),
            ARDA_CAPABILITY("ReservedBuffers",
                C.mResidency.mbReservedBuffers, SparseResidency),
            ARDA_CAPABILITY("ReservedTexture2D",
                C.mResidency.mbReservedTexture2D, SparseResidency),
            ARDA_CAPABILITY("ReservedTexture3D",
                C.mResidency.mbReservedTexture3D, SparseResidency),
            ARDA_CAPABILITY("AliasedSparseMappings",
                C.mResidency.mbAliasedMappings, SparseResidency),
            ARDA_CAPABILITY("StreamingBudget",
                C.mResidency.mbStreamingBudget, StreamingBudget),
            ARDA_CAPABILITY("StreamingBudgetReservation",
                C.mResidency.mbBudgetReservation, StreamingBudget),
            ARDA_CAPABILITY("SparseTileSize",
                C.mResidency.mTileSizeInBytes > 0, SparseResidency),

            ARDA_CAPABILITY("SubgroupOperations",
                C.mMachineLearning.mbSubgroupOperations, QueueBreadth),
            ARDA_CAPABILITY("NativeFloat16",
                C.mMachineLearning.mbNativeFloat16, QueueBreadth),
            ARDA_CAPABILITY("NativeInt8",
                C.mMachineLearning.mbNativeInt8, QueueBreadth),
            ARDA_CAPABILITY("BufferDeviceAddress",
                C.mMachineLearning.mbBufferDeviceAddress, QueueBreadth),
            ARDA_CAPABILITY("MinimumSubgroupSize",
                C.mMachineLearning.mSubgroupMinSize > 0, QueueBreadth),
            ARDA_CAPABILITY("MaximumSubgroupSize",
                C.mMachineLearning.mSubgroupMaxSize > 0, QueueBreadth),

            ARDA_CAPABILITY("MeshShaders",
                C.mMeshShaderTier != arda::rhi::EArdaRHIMeshShaderTier::None,
                MeshShader),
            ARDA_CAPABILITY("WorkGraphs",
                C.mWorkGraphTier != arda::rhi::EArdaRHIWorkGraphTier::None,
                WorkGraph),
            ARDA_CAPABILITY("SamplerFeedback",
                C.mSamplerFeedbackTier !=
                    arda::rhi::EArdaRHISamplerFeedbackTier::None,
                SamplerFeedback),
            ARDA_CAPABILITY("ShaderBundleDispatch",
                C.mbShaderBundleDispatch, ShaderBundle),
            ARDA_CAPABILITY("CustomPresent", C.mbCustomPresent,
                CustomPresent),
            ARDA_CAPABILITY("ResourceCollections", C.mbResourceCollections,
                ExpandedDescriptors),
            ARDA_CAPABILITY("ConservativeRasterization",
                C.mbConservativeRasterization, Contract),
            ARDA_CAPABILITY("VariableRateShading",
                C.mbVariableRateShading, Contract),
            ARDA_CAPABILITY("VirtualResources", C.mbVirtualResources,
                HeapAliasing),
            ARDA_CAPABILITY("ExplicitHeaps", C.mbHeaps, HeapAliasing),
            ARDA_CAPABILITY("StagingTextures", C.mbStagingTextures,
                ExtendedCommands),
            ARDA_CAPABILITY("TextureCopies", C.mbTextureCopies,
                ExtendedCommands),
            ARDA_CAPABILITY("TextureResolve", C.mbTextureResolve, Resolve),
            ARDA_CAPABILITY("ExplicitTransitions", C.mbExplicitTransitions,
                ExtendedCommands),
            ARDA_CAPABILITY("SplitTransitions", C.mbSplitTransitions,
                ExtendedCommands),
            ARDA_CAPABILITY("IndirectCommands", C.mbIndirectCommands,
                ExtendedCommands),
            ARDA_CAPABILITY("AliasingBarriers", C.mbAliasingBarriers,
                HeapAliasing),
            ARDA_CAPABILITY("Queries", C.mbQueries, Queries),
            ARDA_CAPABILITY("ShaderLibraries", C.mbShaderLibraries,
                ShaderLibrary),
            ARDA_CAPABILITY("PipelineCachePersistence",
                C.mbPipelineCachePersistence, Contract)
        };

        static const std::vector<FCapabilityConformanceCase> Cases = []
        {
            std::vector<FCapabilityConformanceCase> Result;
            const auto AddBackend = [&Result](
                arda::backend::EArdaBackendType Backend,
                const char* BackendName,
                const char* BackendLabel)
            {
                for (const FCapabilityDefinition& Definition : Definitions)
                    Result.push_back(
                        {Backend, BackendName, BackendLabel, Definition});
            };
#if defined(_WIN32) && defined(ARDA_TEST_NATIVE_D3D12)
            AddBackend(arda::backend::EArdaBackendType::D3D12,
                "native-d3d12", "D3D12");
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
            AddBackend(arda::backend::EArdaBackendType::Vulkan,
                "native-vulkan", "Vulkan");
#endif
            return Result;
        }();
        return Cases;
    }

#undef ARDA_CAPABILITY

    class FArdaRHICapabilityConformanceTest
        : public testing::TestWithParam<FCapabilityConformanceCase>
    {
    };
}

TEST_P(FArdaRHICapabilityConformanceTest, AdvertisedCapabilityConforms)
{
    using namespace arda::backend;

    const FCapabilityConformanceCase& TestCase = GetParam();
    ShutdownBackend();
    FExtendedBackendCleanup Cleanup;
    FExtendedDiagnosticCallback Diagnostics;
    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = TestCase.mBackendName;
    Configuration.mBackend = TestCase.mBackend;
    Configuration.mbEnableValidation = true;
    Configuration.mMessageCallback = &Diagnostics;
    Configuration.mShaderCompilationMode =
        EArdaShaderCompilationMode::LoadOnly;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    arda::rhi::FArdaRHIDeviceRef Device = GetDevice();
    ASSERT_TRUE(Device);
    const arda::rhi::FArdaRHICapabilities Capabilities =
        Device->GetCapabilities();
    if (!TestCase.mCapability.mIsAdvertised(Capabilities))
    {
        GTEST_SKIP() << TestCase.mBackendLabel << " does not advertise "
            << TestCase.mCapability.mName << " on this adapter.";
    }

    VerifyCapabilityInvariants(Capabilities);
    EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    Device = nullptr;
    ShutdownBackend();
    RunCapabilityProbe(TestCase);
}

INSTANTIATE_TEST_SUITE_P(
    NativeHardware,
    FArdaRHICapabilityConformanceTest,
    testing::ValuesIn(GetCapabilityConformanceCases()),
    [](const testing::TestParamInfo<FCapabilityConformanceCase>& Info)
    {
        return std::string(Info.param.mBackendLabel) + "_" +
            Info.param.mCapability.mName;
    });

#if defined(_WIN32) && defined(ARDA_TEST_NATIVE_D3D12)
TEST(ArdaBackend, D3D12ExtendedCopyTransitionAndIndirectStateParity)
{
    VerifyExtendedCommands(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12ResolveAndFormatPlaneStateParity)
{
    VerifyResolveAndPlaneTracking(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12ExplicitHeapAliasingStateParity)
{
    VerifyExplicitHeapAliasing(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12BindlessDescriptorTableExecutes)
{
    VerifyBindlessDescriptorTable(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12DirectDescriptorHeapIndexingExecutes)
{
    VerifyBindlessDescriptorTable(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12", true);
}

TEST(ArdaBackend, D3D12DirectResourceAndSamplerHeapIndexingExecutes)
{
    VerifyDirectResourceAndSamplerHeapIndexing(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12ShaderBundleExecutes)
{
    VerifyShaderBundleExecution(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12WorkGraphExecutes)
{
    VerifyD3D12WorkGraphExecution();
}

TEST(ArdaBackend, D3D12MeshPipelineCapabilityAndExecution)
{
    VerifyMeshPipelineCapabilityAndExecution(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12QueueBreadthExecutes)
{
    VerifyQueueBreadth(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12RayTracingPipelineCapabilityAndExecution)
{
    VerifyRayTracingPipelineCapabilityAndExecution(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12AccelerationStructureLifecycleAndStateParity)
{
    VerifyAccelerationStructureLifecycleAndStateParity(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12RaySceneHitGroupsLocalArgumentsAndIndirectReadback)
{
    VerifyRayTracingSceneHitGroupsLocalArgumentsAndIndirect(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12ExpandedDescriptorsAndResourceCollections)
{
    VerifyExpandedDescriptorsAndResourceCollections(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12SparseResidencyAndStreamingBudget)
{
    VerifySparseResidencyAndStreamingBudget(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12");
}

TEST(ArdaBackend, D3D12SamplerFeedbackStateParity)
{
    VerifySamplerFeedbackStateParity();
}

TEST(ArdaBackend, D3D12CustomPresentExecutes)
{
    VerifyD3D12CustomPresentExecution();
}

TEST(ArdaBackend, D3D12DeferredSubmissionLifetimeSurvivesImmediateRelease)
{
    VerifyD3D12DeferredSubmissionLifetime();
}
#endif

#if defined(ARDA_TEST_NATIVE_VULKAN)
TEST(ArdaBackend, VulkanExtendedCopyTransitionAndIndirectStateParity)
{
    VerifyExtendedCommands(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}

TEST(ArdaBackend, VulkanResolveAndFormatPlaneStateParity)
{
    VerifyResolveAndPlaneTracking(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}


TEST(ArdaBackend, VulkanExplicitHeapAliasingStateParity)
{
    VerifyExplicitHeapAliasing(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}


TEST(ArdaBackend, VulkanBindlessDescriptorTableExecutes)
{
    VerifyBindlessDescriptorTable(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}

TEST(ArdaBackend, VulkanDirectDescriptorHeapIndexingExecutes)
{
    VerifyBindlessDescriptorTable(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan", true);
}

TEST(ArdaBackend, VulkanDirectResourceAndSamplerHeapIndexingExecutes)
{
    VerifyDirectResourceAndSamplerHeapIndexing(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}

TEST(ArdaBackend, VulkanShaderBundleExecutes)
{
    VerifyShaderBundleExecution(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}


TEST(ArdaBackend, VulkanMeshPipelineCapabilityAndExecution)
{
    VerifyMeshPipelineCapabilityAndExecution(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}

TEST(ArdaBackend, VulkanQueueBreadthExecutes)
{
    VerifyQueueBreadth(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}

TEST(ArdaBackend, VulkanRayTracingPipelineCapabilityAndExecution)
{
    VerifyRayTracingPipelineCapabilityAndExecution(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}


TEST(ArdaBackend, VulkanAccelerationStructureLifecycleAndStateParity)
{
    VerifyAccelerationStructureLifecycleAndStateParity(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}

TEST(ArdaBackend, VulkanRaySceneHitGroupsLocalArgumentsAndIndirectReadback)
{
    VerifyRayTracingSceneHitGroupsLocalArgumentsAndIndirect(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}

TEST(ArdaBackend, VulkanOpacityMicromapLifecycleAndStateParity)
{
    VerifyVulkanOpacityMicromapLifecycleAndStateParity();
}


TEST(ArdaBackend, VulkanExpandedDescriptorsAndResourceCollections)
{
    VerifyExpandedDescriptorsAndResourceCollections(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}

TEST(ArdaBackend, VulkanSparseResidencyAndStreamingBudget)
{
    VerifySparseResidencyAndStreamingBudget(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan");
}

#if defined(_WIN32)
TEST(ArdaBackend, VulkanCustomPresentExecutes)
{
    VerifyVulkanCustomPresentExecution();
}
#endif
#endif
