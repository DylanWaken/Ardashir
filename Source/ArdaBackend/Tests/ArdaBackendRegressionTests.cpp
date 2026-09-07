#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "PipelineStateCache/ArdaPipelineStateCache.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
    using namespace arda::backend;
    using namespace arda::rhi;

    class FArdaRegressionDiagnostics final : public IArdaDiagnosticCallback
    {
    public:
        void Message(EArdaDiagnosticSeverity Severity, const char* Text) override
        {
            if (Severity >= EArdaDiagnosticSeverity::Error)
            {
                ++mErrors;
            }
            if (Text && Severity >= EArdaDiagnosticSeverity::Warning)
            {
                std::fprintf(stderr, "%s\n", Text);
            }
        }
        std::atomic<uint32_t> mErrors{0};
    };

    class FArdaBackendRegressionTest : public testing::TestWithParam<const char*>
    {
    protected:
        void SetUp() override
        {
            ShutdownBackend();
            FArdaBackendConfiguration Configuration;
            Configuration.mBackendName = GetParam();
            Configuration.mbEnableValidation = true;
            Configuration.mMessageCallback = &mDiagnostics;
            Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
            ASSERT_TRUE(ConfigureBackend(Configuration));
            ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
            mDevice = GetDevice();
            ASSERT_TRUE(mDevice);
        }

        void TearDown() override
        {
            if (mDevice)
            {
                EXPECT_TRUE(mDevice->WaitForIdle());
            }
            mDevice = {};
            ShutdownBackend();
            if (GetBackendInitializeResult() != EArdaInitializeResult::ValidationUnavailable)
                EXPECT_EQ(mDiagnostics.mErrors.load(), 0u);
        }

        FArdaRegressionDiagnostics mDiagnostics;
        FArdaRHIDeviceRef mDevice;
    };

    TEST_P(FArdaBackendRegressionTest, IntegerBufferClearPreservesStateAndWritesEveryWord)
    {
        FArdaRHIBufferDesc Desc;
        Desc.mByteSize = 1024;
        Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
        Desc.mInitialState = EArdaRHIResourceState::Common;
        auto Buffer = mDevice->CreateBuffer(Desc);
        ASSERT_TRUE(Buffer);
        auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ASSERT_TRUE(Commands.mValue->ClearBufferUInt(*Buffer.mValue, 0x12345678u));
        auto State = Commands.mValue->QueryBufferState(*Buffer.mValue);
        ASSERT_TRUE(State);
        EXPECT_TRUE(State.mValue.IsConsistent());
        EXPECT_EQ(State.mValue.mFacadeState, Desc.mInitialState);
        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(
            *Buffer.mValue, Readback, 0, Desc.mByteSize));
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(mDevice->WaitForIdle());
        ASSERT_EQ(Readback.size(), Desc.mByteSize);
        for (size_t Offset = 0; Offset < Readback.size(); Offset += sizeof(uint32_t))
        {
            uint32_t Value = 0;
            std::memcpy(&Value, Readback.data() + Offset, sizeof(Value));
            EXPECT_EQ(Value, 0x12345678u) << Offset;
        }
    }

    TEST_P(FArdaBackendRegressionTest, IntegerTextureClearHonorsMipAndArrayRange)
    {
        FArdaRHITextureDesc Desc;
        Desc.mWidth = 8;
        Desc.mHeight = 8;
        Desc.mMipLevels = 2;
        Desc.mArraySize = 2;
        Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
        Desc.mFormat = EArdaRHIFormat::R32UInt;
        Desc.mUsage = EArdaRHITextureUsage::UnorderedAccess;
        Desc.mInitialState = EArdaRHIResourceState::Common;
        auto Texture = mDevice->CreateTexture(Desc);
        ASSERT_TRUE(Texture);
        FArdaRHIStagingTextureDesc StagingDesc;
        StagingDesc.mTexture = Desc;
        StagingDesc.mCpuAccess = EArdaRHICpuAccess::Read;
        auto Staging = mDevice->CreateStagingTexture(StagingDesc);
        ASSERT_TRUE(Staging);
        auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands);
        ASSERT_TRUE(Commands.mValue->Open());
        ASSERT_TRUE(Commands.mValue->ClearTextureUInt(*Texture.mValue, {}, 0x10203040u));
        ASSERT_TRUE(Commands.mValue->ClearTextureUInt(*Texture.mValue, {1, 1, 1, 1}, 0xabcdef12u));
        auto State = Commands.mValue->QueryTextureState(*Texture.mValue, {});
        ASSERT_TRUE(State);
        EXPECT_TRUE(State.mValue.IsConsistent());
        EXPECT_EQ(State.mValue.mFacadeState, Desc.mInitialState);
        for (uint32_t Slice = 0; Slice < Desc.mArraySize; ++Slice)
        {
            for (uint32_t Mip = 0; Mip < Desc.mMipLevels; ++Mip)
            {
                FArdaRHITextureSlice Region;
                Region.mMipLevel = Mip;
                Region.mArraySlice = Slice;
                Region.mWidth = Desc.mWidth >> Mip;
                Region.mHeight = Desc.mHeight >> Mip;
                Region.mDepth = 1;
                ASSERT_TRUE(Commands.mValue->CopyTextureToStaging(
                    *Staging.mValue, Region, *Texture.mValue, Region));
            }
        }
        ASSERT_TRUE(Commands.mValue->Close());
        ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
        ASSERT_TRUE(mDevice->WaitForIdle());
        for (uint32_t Slice = 0; Slice < Desc.mArraySize; ++Slice)
        {
            for (uint32_t Mip = 0; Mip < Desc.mMipLevels; ++Mip)
            {
                FArdaRHITextureSlice Region;
                Region.mMipLevel = Mip;
                Region.mArraySlice = Slice;
                auto Mapping = mDevice->MapStagingTexture(Staging.mValue, Region, EArdaRHICpuAccess::Read);
                ASSERT_TRUE(Mapping);
                for (uint32_t Y = 0; Y < (Desc.mHeight >> Mip); ++Y)
                {
                    for (uint32_t X = 0; X < (Desc.mWidth >> Mip); ++X)
                    {
                        uint32_t Value = 0;
                        std::memcpy(&Value, static_cast<const uint8_t*>(Mapping.mValue.mData) +
                            Y * Mapping.mValue.mRowPitch + X * sizeof(Value), sizeof(Value));
                        EXPECT_EQ(Value, Mip == 1 && Slice == 1 ? 0xabcdef12u : 0x10203040u);
                    }
                }
                ASSERT_TRUE(mDevice->UnmapStagingTexture(Staging.mValue));
            }
        }
    }

    TEST_P(FArdaBackendRegressionTest, UnboundedSamplerCapacitySupportsReplacementVersions)
    {
        const auto& Caps = mDevice->GetCapabilities().mDescriptors;
        if (!Caps.mbUnboundedArrays)
        {
            GTEST_SKIP() << "Unbounded descriptor arrays are unavailable.";
        }
        FArdaRHIBindlessLayoutDesc Desc;
        Desc.mVisibility = EArdaRHIShaderStage::Compute;
        Desc.mbUnbounded = true;
        Desc.mLayoutType = EArdaRHIBindlessLayoutType::MutableSampler;
        Desc.mRegisterSpaces.push_back({0, 1, EArdaRHIBindingType::Sampler});
        auto Layout = mDevice->CreateBindlessLayout(Desc);
        ASSERT_TRUE(Layout);
        EXPECT_EQ(Layout.mValue->GetDesc().mItems.front().mArraySize, Caps.mMaxSamplerDescriptors);
        EXPECT_EQ(Layout.mValue->GetBindlessDesc()->mMaxCapacity, Caps.mMaxSamplerDescriptors);
        auto Table = mDevice->CreateDescriptorTable(Layout.mValue);
        ASSERT_TRUE(Table) << Table.mStatus.mMessage.c_str();
        auto Sampler = mDevice->CreateSampler({});
        ASSERT_TRUE(Sampler);
        FArdaRHIBindingItem Item;
        Item.mType = EArdaRHIBindingType::Sampler;
        Item.mArrayElement = Caps.mMaxSamplerDescriptors - 1;
        Item.mResource = FArdaRHIResourceRef(Sampler.mValue.Get());
        for (uint32_t Version = 0; Version < 3; ++Version)
        {
            ASSERT_TRUE(mDevice->WriteDescriptorTable(Table.mValue, Item));
        }
        Desc.mMaxCapacity = Caps.mMaxSamplerDescriptors + 1;
        const auto Overflow = mDevice->CreateBindlessLayout(Desc);
        EXPECT_FALSE(Overflow);
        EXPECT_EQ(Overflow.mStatus.mCode, EArdaRHIResult::InvalidArgument);
    }

    TEST_P(FArdaBackendRegressionTest, PersistentPipelineKeyIncludesBindlessLayoutAbi)
    {
        ASSERT_TRUE(mDevice);
        ASSERT_TRUE(mDevice->GetCapabilities().mDescriptors.mbDirectResourceHeapIndexing);
        const std::string Path = std::string(ARDA_BACKEND_TEST_SHADER_DIR "/") +
            "ArdaShaderStructTest" + GetShaderArtifactExtension(GetParam());
        std::ifstream Stream(Path, std::ios::binary);
        ASSERT_TRUE(Stream) << Path;
        const std::vector<char> Bytes((std::istreambuf_iterator<char>(Stream)), {});
        ASSERT_FALSE(Bytes.empty());
        FArdaRHIShaderDesc ShaderDesc;
        ShaderDesc.mStage = EArdaRHIShaderStage::Compute;
        ShaderDesc.mEntryPoint = "ShaderStructTestCS";
        ShaderDesc.mBytecode = Bytes.data();
        ShaderDesc.mBytecodeSize = Bytes.size();
        auto Shader = mDevice->CreateShader(ShaderDesc);
        ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();

        FArdaRHIBindlessLayoutDesc Desc;
        Desc.mVisibility = EArdaRHIShaderStage::Compute;
        Desc.mMaxCapacity = 4;
        Desc.mLayoutType = EArdaRHIBindlessLayoutType::MutableSrvUavCbv;
        Desc.mRegisterSpaces.push_back({0, 1, EArdaRHIBindingType::StructuredBufferUAV});
        auto ConventionalLayout = mDevice->CreateBindlessLayout(Desc);
        ASSERT_TRUE(ConventionalLayout);
        Desc.mbDirectHeapIndexing = true;
        auto DirectLayout = mDevice->CreateBindlessLayout(Desc);
        ASSERT_TRUE(DirectLayout);

        FArdaPipelineStateCache Cache(mDevice);
        FArdaComputePipelineStateInitializer Initializer;
        Initializer.mDesc.mComputeShader = Shader.mValue;
        Initializer.mDesc.mBindingLayouts = {ConventionalLayout.mValue};
        FArdaRHIComputePipelineRef Conventional;
        auto Status = Cache.GetOrCreateCompute(Initializer, Conventional);
        ASSERT_TRUE(Status) << Status.mMessage.c_str();
        Initializer.mDesc.mBindingLayouts = {DirectLayout.mValue};
        FArdaRHIComputePipelineRef Direct;
        Status = Cache.GetOrCreateCompute(Initializer, Direct);
        ASSERT_TRUE(Status) << Status.mMessage.c_str();
        EXPECT_NE(Conventional->GetDesc().mPersistentCacheKey,
            Direct->GetDesc().mPersistentCacheKey);
    }

    const char* const NativeBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
        "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
        "native-vulkan",
#endif
    };
    INSTANTIATE_TEST_SUITE_P(NativeProviders, FArdaBackendRegressionTest,
        testing::ValuesIn(NativeBackends));
}
