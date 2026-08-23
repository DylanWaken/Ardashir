#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>

namespace
{
    class FStateDiagnosticCallback final
        : public arda::backend::IArdaDiagnosticCallback
    {
    public:
        void Message(
            arda::backend::EArdaDiagnosticSeverity Severity,
            const char*) override
        {
            if (Severity == arda::backend::EArdaDiagnosticSeverity::Error ||
                Severity == arda::backend::EArdaDiagnosticSeverity::Fatal)
            {
                mErrorCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] uint32_t GetErrorCount() const noexcept
        {
            return mErrorCount.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<uint32_t> mErrorCount{0};
    };

    class FStateBackendCleanup final
    {
    public:
        ~FStateBackendCleanup()
        {
            arda::backend::ShutdownBackend();
        }
    };

    void VerifyNativeResourceStateConformance(
        arda::backend::EArdaBackendType Backend,
        const char* BackendName,
        arda::rhi::EArdaRHINativeResourceType TextureNativeType,
        arda::rhi::EArdaRHINativeResourceType BufferNativeType)
    {
        using namespace arda::backend;
        using namespace arda::rhi;

        ShutdownBackend();
        FStateBackendCleanup Cleanup;
        FStateDiagnosticCallback Diagnostics;
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

        FArdaRHITextureDesc TextureDesc;
        TextureDesc.mDebugName = "State conformance texture";
        TextureDesc.mWidth = 8;
        TextureDesc.mHeight = 8;
        TextureDesc.mArraySize = 2;
        TextureDesc.mMipLevels = 2;
        TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
        TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource |
            EArdaRHITextureUsage::UnorderedAccess;
        TextureDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Texture = Device->CreateTexture(TextureDesc);
        ASSERT_TRUE(Texture);

        FArdaRHIBufferDesc BufferDesc;
        BufferDesc.mDebugName = "State conformance buffer";
        BufferDesc.mByteSize = 256;
        BufferDesc.mUsage = EArdaRHIBufferUsage::ShaderResource |
            EArdaRHIBufferUsage::UnorderedAccess;
        BufferDesc.mInitialState = EArdaRHIResourceState::Common;
        auto Buffer = Device->CreateBuffer(BufferDesc);
        ASSERT_TRUE(Buffer);

        auto Commands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        Commands.mValue->SetAutomaticBarriers(false);

        const FArdaRHITextureSubresourceRange Mip0Slice0{0, 1, 0, 1};
        const FArdaRHITextureSubresourceRange Mip1Slice1{1, 1, 1, 1};
        auto InitialTexture = Commands.mValue->QueryTextureState(
            *Texture.mValue, {});
        ASSERT_TRUE(InitialTexture);
        EXPECT_TRUE(InitialTexture.mValue.IsConsistent());
        EXPECT_EQ(
            InitialTexture.mValue.mFacadeState,
            EArdaRHIResourceState::Common);
        EXPECT_EQ(
            InitialTexture.mValue.mNative.mNativeType,
            TextureNativeType);

        ASSERT_TRUE(Commands.mValue->SetTextureState(
            *Texture.mValue,
            Mip0Slice0,
            EArdaRHIResourceState::UnorderedAccess));
        auto UavTexture = Commands.mValue->QueryTextureState(
            *Texture.mValue, Mip0Slice0);
        ASSERT_TRUE(UavTexture);
        EXPECT_TRUE(UavTexture.mValue.IsConsistent());
        EXPECT_EQ(
            UavTexture.mValue.mNative.mState,
            EArdaRHIResourceState::UnorderedAccess);
        EXPECT_NE(
            UavTexture.mValue.mNative.mPrimaryState,
            InitialTexture.mValue.mNative.mPrimaryState);
        EXPECT_TRUE(Commands.mValue->AssertTextureState(
            *Texture.mValue,
            Mip0Slice0,
            EArdaRHIResourceState::UnorderedAccess));

        auto UntouchedTexture = Commands.mValue->QueryTextureState(
            *Texture.mValue, Mip1Slice1);
        ASSERT_TRUE(UntouchedTexture);
        EXPECT_TRUE(UntouchedTexture.mValue.IsConsistent());
        EXPECT_EQ(
            UntouchedTexture.mValue.mFacadeState,
            EArdaRHIResourceState::Common);
        EXPECT_EQ(
            UntouchedTexture.mValue.mNative.mPrimaryState,
            InitialTexture.mValue.mNative.mPrimaryState);
        const auto MixedTexture = Commands.mValue->QueryTextureState(
            *Texture.mValue, {});
        EXPECT_FALSE(MixedTexture);
        EXPECT_EQ(MixedTexture.mStatus.mCode, EArdaRHIResult::InvalidState);

        const FArdaRHIStatus WrongStart =
            Commands.mValue->AssertTextureState(
                *Texture.mValue,
                Mip0Slice0,
                EArdaRHIResourceState::CopyDest);
        EXPECT_FALSE(WrongStart);
        EXPECT_EQ(WrongStart.mCode, EArdaRHIResult::InvalidState);
        EXPECT_TRUE(Commands.mValue->AssertTextureState(
            *Texture.mValue,
            Mip0Slice0,
            EArdaRHIResourceState::UnorderedAccess));

        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *Buffer.mValue, EArdaRHIResourceState::CopyDest));
        auto CopyBuffer = Commands.mValue->QueryBufferState(*Buffer.mValue);
        ASSERT_TRUE(CopyBuffer);
        EXPECT_TRUE(CopyBuffer.mValue.IsConsistent());
        EXPECT_EQ(CopyBuffer.mValue.mNative.mNativeType, BufferNativeType);
        EXPECT_EQ(
            CopyBuffer.mValue.mFacadeState,
            EArdaRHIResourceState::CopyDest);

        ASSERT_TRUE(Commands.mValue->SetBufferState(
            *Buffer.mValue, EArdaRHIResourceState::ShaderResource));
        auto ReadBuffer = Commands.mValue->QueryBufferState(*Buffer.mValue);
        ASSERT_TRUE(ReadBuffer);
        EXPECT_TRUE(ReadBuffer.mValue.IsConsistent());
        EXPECT_NE(
            ReadBuffer.mValue.mNative.mPrimaryState |
                ReadBuffer.mValue.mNative.mPipelineStageMask |
                ReadBuffer.mValue.mNative.mAccessMask,
            CopyBuffer.mValue.mNative.mPrimaryState |
                CopyBuffer.mValue.mNative.mPipelineStageMask |
                CopyBuffer.mValue.mNative.mAccessMask);
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());

        auto NextCommands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(NextCommands);
        ASSERT_TRUE(NextCommands.mValue->Open());
        auto PersistedTexture = NextCommands.mValue->QueryTextureState(
            *Texture.mValue, Mip0Slice0);
        auto PersistedBuffer = NextCommands.mValue->QueryBufferState(
            *Buffer.mValue);
        ASSERT_TRUE(PersistedTexture);
        ASSERT_TRUE(PersistedBuffer);
        EXPECT_TRUE(PersistedTexture.mValue.IsConsistent());
        EXPECT_TRUE(PersistedBuffer.mValue.IsConsistent());
        EXPECT_EQ(
            PersistedTexture.mValue.mFacadeState,
            EArdaRHIResourceState::UnorderedAccess);
        EXPECT_EQ(
            PersistedBuffer.mValue.mFacadeState,
            EArdaRHIResourceState::ShaderResource);
        ASSERT_TRUE(NextCommands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(NextCommands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());

        auto CopyCommands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(CopyCommands);
        ASSERT_TRUE(CopyCommands.mValue->Open());
        const uint32_t Pattern = 0x1234abcd;
        ASSERT_TRUE(CopyCommands.mValue->WriteBuffer(
            *Buffer.mValue, &Pattern, sizeof(Pattern)));
        auto RestoredAfterWrite =
            CopyCommands.mValue->QueryBufferState(*Buffer.mValue);
        ASSERT_TRUE(RestoredAfterWrite);
        EXPECT_TRUE(RestoredAfterWrite.mValue.IsConsistent());
        EXPECT_EQ(
            RestoredAfterWrite.mValue.mFacadeState,
            EArdaRHIResourceState::ShaderResource);

        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(CopyCommands.mValue->CopyBufferDeviceToHost(
            *Buffer.mValue, Readback, 0, sizeof(Pattern)));
        auto ReadbackSource =
            CopyCommands.mValue->QueryBufferState(*Buffer.mValue);
        ASSERT_TRUE(ReadbackSource);
        EXPECT_TRUE(ReadbackSource.mValue.IsConsistent());
        EXPECT_EQ(
            ReadbackSource.mValue.mFacadeState,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(CopyCommands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(CopyCommands.mValue));
        ASSERT_EQ(Readback.size(), sizeof(Pattern));
        uint32_t ReadbackPattern = 0;
        std::memcpy(&ReadbackPattern, Readback.data(), sizeof(ReadbackPattern));
        EXPECT_EQ(ReadbackPattern, Pattern);

        auto WrongStartCommands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(WrongStartCommands);
        ASSERT_TRUE(WrongStartCommands.mValue->Open());
        WrongStartCommands.mValue->SetAutomaticBarriers(false);
        ASSERT_TRUE(WrongStartCommands.mValue->BeginTrackingTextureState(
            *Texture.mValue,
            Mip0Slice0,
            EArdaRHIResourceState::Common));
        ASSERT_TRUE(WrongStartCommands.mValue->SetTextureState(
            *Texture.mValue,
            Mip0Slice0,
            EArdaRHIResourceState::ShaderResource));
        ASSERT_TRUE(WrongStartCommands.mValue->Close());
        const auto RejectedSubmission =
            Device->ExecuteCommandList(WrongStartCommands.mValue);
        EXPECT_FALSE(RejectedSubmission);
        EXPECT_EQ(
            RejectedSubmission.mStatus.mCode,
            EArdaRHIResult::InvalidState);

        auto VerificationCommands = Device->CreateCommandList(
            EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(VerificationCommands);
        ASSERT_TRUE(VerificationCommands.mValue->Open());
        auto UnchangedAfterReject =
            VerificationCommands.mValue->QueryTextureState(
                *Texture.mValue, Mip0Slice0);
        auto PersistedReadbackSource =
            VerificationCommands.mValue->QueryBufferState(*Buffer.mValue);
        ASSERT_TRUE(UnchangedAfterReject);
        ASSERT_TRUE(PersistedReadbackSource);
        EXPECT_EQ(
            UnchangedAfterReject.mValue.mFacadeState,
            EArdaRHIResourceState::UnorderedAccess);
        EXPECT_TRUE(UnchangedAfterReject.mValue.IsConsistent());
        EXPECT_TRUE(PersistedReadbackSource.mValue.IsConsistent());
        EXPECT_EQ(
            PersistedReadbackSource.mValue.mFacadeState,
            EArdaRHIResourceState::CopySource);
        ASSERT_TRUE(VerificationCommands.mValue->Close());
        ASSERT_TRUE(Device->ExecuteCommandList(VerificationCommands.mValue));
        ASSERT_TRUE(Device->WaitForIdle());
        EXPECT_EQ(Diagnostics.GetErrorCount(), 0u);
    }
}

#if defined(_WIN32) && defined(ARDA_TEST_NATIVE_D3D12)
TEST(ArdaBackend, D3D12ResourceStateMatchesFacadeAndNativeEncodingAtEveryStep)
{
    VerifyNativeResourceStateConformance(
        arda::backend::EArdaBackendType::D3D12,
        "native-d3d12",
        arda::rhi::EArdaRHINativeResourceType::D3D12Resource,
        arda::rhi::EArdaRHINativeResourceType::D3D12Resource);
}
#endif

#if defined(ARDA_TEST_NATIVE_VULKAN)
TEST(ArdaBackend, VulkanResourceStateMatchesFacadeAndNativeEncodingAtEveryStep)
{
    VerifyNativeResourceStateConformance(
        arda::backend::EArdaBackendType::Vulkan,
        "native-vulkan",
        arda::rhi::EArdaRHINativeResourceType::VulkanImage,
        arda::rhi::EArdaRHINativeResourceType::VulkanBuffer);
}
#endif
