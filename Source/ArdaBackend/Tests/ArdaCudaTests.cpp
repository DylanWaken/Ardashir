#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaTestComputeOperand.h"
#include "ArdaRenderGraph.h"
#include <gtest/gtest.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace arda;
    class FCudaDiagnostics : public IArdaDiagnosticCallback
    {
    public:
        std::atomic<uint32_t> mErrors{0};
        void Message(EArdaDiagnosticSeverity Severity, const char* Text) override
        {
            if (Severity == EArdaDiagnosticSeverity::Error || Severity == EArdaDiagnosticSeverity::Fatal)
            { ++mErrors; std::fprintf(stderr, "CUDA validation: %s\n", Text); }
        }
    };
    class ArdaCudaGpu : public testing::TestWithParam<const char*>
    {
    protected:
        FCudaDiagnostics mDiagnostics;
        FArdaRHIDeviceRef mDevice;
        void SetUp() override
        {
            ShutdownBackend();
            FArdaBackendConfiguration C;
            const eastl::string Name = GetParam();
            C.mBackendName = Name.find("d3d12") != eastl::string::npos ? "native-d3d12" : "native-vulkan";
            C.mCudaExecutionMode = Name.find("context") != eastl::string::npos
                ? EArdaCudaExecutionMode::ContextSwitch : EArdaCudaExecutionMode::GraphicsQueue;
            if (!FindBackendModule(C.mBackendName.c_str())) GTEST_SKIP() << "Backend not built";
            C.mbEnableValidation = true;
            C.mMessageCallback = &mDiagnostics;
            C.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
            ASSERT_TRUE(ConfigureBackend(C));
            ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
            mDevice = GetDevice();
            const auto Caps = mDevice->GetCudaCapabilities();
            if (!Caps) GTEST_SKIP() << Caps.mUnavailableReason.c_str();
            FArdaAddOperand Operand(mDevice);
            auto Support = Operand.GetOperandSupport();
            if (!Support) GTEST_SKIP() << Support.mMessage.c_str();
            std::printf("%s: CUDA mode=%u SM=%u\n", GetParam(), unsigned(Caps.mLaunchMode), Caps.mComputeCapability);
        }
        void TearDown() override
        {
            if (mDevice) EXPECT_TRUE(mDevice->WaitForIdle());
            mDevice.Reset(); ShutdownBackend();
            EXPECT_EQ(mDiagnostics.mErrors.load(), 0u);
        }
        FArdaRHIBufferDesc BufferDesc(uint64_t Size, bool Cuda = true)
        {
            FArdaRHIBufferDesc D;
            D.mByteSize = Size; D.mbCudaInterop = Cuda; D.mDebugName = "CUDA test buffer";
            D.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
            return D;
        }
        void ExpectWords(const eastl::vector<uint8_t>& Bytes, uint32_t Count, uint32_t Bias)
        {
            ASSERT_EQ(Bytes.size(), size_t(Count) * 4);
            for (uint32_t I = 0; I < Count; ++I)
            { uint32_t V; std::memcpy(&V, Bytes.data() + I * 4, 4); ASSERT_EQ(V, I * 3 + Bias) << I; }
        }
    };

    TEST_P(ArdaCudaGpu, PrecompiledDeferredAndImmediateUseFrozenSingleKernelPlans)
    {
        for (uint32_t Count : {32u, 128u})
        {
            FArdaAddOperand Operand(mDevice);
            Operand.mbPreferFastMath = Count == 128;
            auto Input = mDevice->CreateBuffer(BufferDesc(Count * 4));
            auto Output = mDevice->CreateBuffer(BufferDesc(Count * 4));
            ASSERT_TRUE(Input); ASSERT_TRUE(Output);
            FArdaAddParameters P;
            P.mInput.mBuffer = Input.mValue; P.mOutput.mBuffer = Output.mValue;
            P.mCount = Count; P.mBias = 7;
            auto Plan = Operand.PrepareDispatch(P);
            ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
            ASSERT_EQ(Plan.mValue->mDispatch.mKernels.size(), 1u);
            if (mDevice->GetCudaCapabilities().mComputeCapability == 120)
                EXPECT_EQ(Plan.mValue->mDispatch.mKernels.front().mEntry->GetBuildInfo().mbFastMath, Count == 128);
            EXPECT_EQ(Plan.mValue->mSelection.mLaunch.mBlockSize[0], Count == 32 ? 32u : 128u);
            P.mBias = 999; // Recorded plan owns a snapshot, not this mutable host object.
            auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
            ASSERT_TRUE(Cmd); ASSERT_TRUE(Cmd.mValue->Open());
            eastl::vector<uint32_t> Values(Count);
            for (uint32_t I = 0; I < Count; ++I) Values[I] = I * 3;
            ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Input.mValue, Values.data(), Values.size() * 4));
            auto Status = Operand.RecordPlan(*Cmd.mValue, Plan.mValue);
            ASSERT_TRUE(Status) << Status.mMessage.c_str();
            eastl::vector<uint8_t> Bytes;
            ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Output.mValue, Bytes));
            ASSERT_TRUE(Cmd.mValue->Close());
            auto Submitted = mDevice->ExecuteCommandList(Cmd.mValue);
            ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
            ASSERT_TRUE(mDevice->WaitForIdle());
            ExpectWords(Bytes, Count, 7);
            EXPECT_FALSE(mDevice->ExecuteCommandList(Cmd.mValue)); // CUDA lists are single-use.
            P.mInput.mBuffer = Output.mValue; P.mBias = 11;
            auto Immediate = Operand.Dispatch(P);
            ASSERT_TRUE(Immediate) << Immediate.mStatus.mMessage.c_str();
            EXPECT_GT(Immediate.mValue.mInstance, 0u);
            Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
            ASSERT_TRUE(Cmd); ASSERT_TRUE(Cmd.mValue->Open());
            ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Output.mValue, Bytes));
            ASSERT_TRUE(Cmd.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
            ASSERT_TRUE(mDevice->WaitForIdle());
            ExpectWords(Bytes, Count, 18);
        }
    }

    TEST_P(ArdaCudaGpu, ResourceChecksAreRuntimeAndBufferOffsetsPreserveGuards)
    {
        FArdaAddOperand Operand(mDevice);
        auto Buffer = mDevice->CreateBuffer(BufferDesc(80 * 4)); ASSERT_TRUE(Buffer);
        auto Normal = mDevice->CreateBuffer(BufferDesc(80 * 4, false)); ASSERT_TRUE(Normal);
        FArdaAddParameters P;
        P.mCount = 32; P.mBias = 7;
        EXPECT_FALSE(Operand.PrepareDispatch(P));
        P.mInput = {Normal.mValue, {16, 128}}; P.mOutput = {Buffer.mValue, {160, 128}};
        EXPECT_EQ(Operand.PrepareDispatch(P).mStatus.mCode, EArdaRHIResult::Unsupported);
        P.mInput = {Buffer.mValue, {17, 128}}; EXPECT_FALSE(Operand.PrepareDispatch(P));
        P.mInput.mRange = {300, 128}; EXPECT_FALSE(Operand.PrepareDispatch(P));
        P.mInput.mRange = {16, 128}; P.mOutput.mRange = {20, 128}; EXPECT_FALSE(Operand.PrepareDispatch(P));
        P.mOutput.mRange = {160, 128};
        auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Cmd); ASSERT_TRUE(Cmd.mValue->Open());
        eastl::vector<uint32_t> Values(80, 0xdeadbeefu);
        for (uint32_t I = 0; I < 32; ++I) Values[I + 4] = I * 3;
        ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
        auto S = Operand.DispatchDeferred(*Cmd.mValue, P); ASSERT_TRUE(S) << S.mMessage.c_str();
        eastl::vector<uint8_t> Bytes;
        ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
        ASSERT_TRUE(Cmd.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
        ASSERT_TRUE(mDevice->WaitForIdle());
        ASSERT_EQ(Bytes.size(), Values.size() * 4);
        for (uint32_t I = 0; I < Values.size(); ++I)
        {
            uint32_t V; std::memcpy(&V, Bytes.data() + I * 4, 4);
            EXPECT_EQ(V, I >= 40 && I < 72 ? (I - 40) * 3 + 7 : Values[I]) << I;
        }
    }

    TEST_P(ArdaCudaGpu, SurfaceConversionAndMipLifetime)
    {
        const auto Caps = mDevice->GetCudaCapabilities();
        if (!Caps.mbSurfaceAccess) GTEST_SKIP() << Caps.mSurfaceUnavailableReason.c_str();
        FArdaRHITextureDesc D;
        D.mbCudaInterop = true; D.mWidth = 54; D.mHeight = 38; D.mMipLevels = 2;
        D.mFormat = EArdaRHIFormat::R32UInt;
        D.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
        auto Texture = mDevice->CreateTexture(D); ASSERT_TRUE(Texture) << Texture.mStatus.mMessage.c_str();
        FArdaRHIStagingTextureDesc Staging;
        Staging.mTexture = D; Staging.mTexture.mbCudaInterop = false; Staging.mCpuAccess = EArdaRHICpuAccess::Read;
        auto Readback = mDevice->CreateStagingTexture(Staging); ASSERT_TRUE(Readback);
        auto Operand = eastl::make_shared<FArdaSurfaceOperand>(mDevice);
        FArdaSurfaceParameters P;
        P.mSurface.mTexture = Texture.mValue; P.mSurface.mRange.mBaseMipLevel = 1;
        P.mSurface.mRange.mMipLevelCount = 1;
        P.mWidth = 27; P.mHeight = 19; P.mValue = 101;
        P.mSurface.mRange.mMipLevelCount = 99; EXPECT_FALSE(Operand->PrepareDispatch(P));
        P.mSurface.mRange.mMipLevelCount = 1;
        auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Cmd); ASSERT_TRUE(Cmd.mValue->Open());
        auto S = Operand->DispatchDeferred(*Cmd.mValue, P); ASSERT_TRUE(S) << S.mMessage.c_str();
        FArdaRHITextureSlice Slice; Slice.mMipLevel = 1;
        ASSERT_TRUE(Cmd.mValue->CopyTextureToStaging(*Readback.mValue, Slice, *Texture.mValue, Slice));
        ASSERT_TRUE(Cmd.mValue->Close());
        P.mSurface.mTexture.Reset(); Texture.mValue.Reset(); Operand.reset();
        auto Submitted = mDevice->ExecuteCommandList(Cmd.mValue);
        ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
        Cmd.mValue.Reset(); ASSERT_TRUE(mDevice->WaitForIdle());
        auto Mapped = mDevice->MapStagingTexture(Readback.mValue, Slice, EArdaRHICpuAccess::Read); ASSERT_TRUE(Mapped);
        for (uint32_t Y = 0; Y < 19; ++Y)
        {
            const auto* Row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(Mapped.mValue.mData) + Y * Mapped.mValue.mRowPitch);
            for (uint32_t X = 0; X < 27; ++X) EXPECT_EQ(Row[X], 101u);
        }
        mDevice->UnmapStagingTexture(Readback.mValue);
    }

    TEST_P(ArdaCudaGpu, RdgDerivesDependenciesBeforeAllocatingPhysicalResources)
    {
        auto Operand = eastl::make_shared<FArdaAddOperand>(mDevice);
        FARDGBuilder Graph(MakeRenderGraphContext(mDevice));
        auto Input = Graph.CreateBuffer(BufferDesc(128 * 4));
        auto Middle = Graph.CreateBuffer(BufferDesc(128 * 4));
        auto Output = Graph.CreateBuffer(BufferDesc(128 * 4));
        eastl::vector<uint32_t> Values(128);
        for (uint32_t I = 0; I < Values.size(); ++I) Values[I] = I * 3;
        (void)Graph.QueueBufferUpload(Input, Values.data(), Values.size() * 4);
        TARDGCudaParameters<FArdaAddParameters> P;
        P.mInput.mBuffer = Input; P.mOutput.mBuffer = Middle; P.mCount = 128; P.mBias = 7;
        EXPECT_FALSE(AddArdaCudaPass(Graph, "invalid pipeline", Operand, P, EARDGPassFlags::None));
        auto Invalid = P;
        Invalid.mInput.mBuffer = Graph.CreateBuffer(BufferDesc(128 * 4, false));
        EXPECT_FALSE(AddArdaCudaPass(Graph, "unshared input", Operand, Invalid));
        auto First = AddArdaCudaPass(Graph, "add seven", Operand, P); ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
        P.mInput.mBuffer = Middle; P.mOutput.mBuffer = Output; P.mBias = 11;
        auto Second = AddArdaCudaPass(Graph, "add eleven", Operand, P); ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
        eastl::vector<uint8_t> Bytes;
        (void)Graph.AddDeviceToHostCopyPass(Output, Bytes);
        (void)Graph.Compile();
        EXPECT_FALSE(Graph.TryGetPass(First.mValue)->GetState().mbCulled);
        EXPECT_FALSE(Graph.TryGetPass(Second.mValue)->GetState().mbCulled);
        const auto& Result = Graph.Execute();
        ASSERT_TRUE(Result.mStatus) << Result.mStatus.mMessage.c_str();
        EXPECT_GT(Result.mSubmittedCommandListCount, 0u);
        ASSERT_TRUE(mDevice->WaitForIdle());
        ExpectWords(Bytes, 128, 18);
    }

    INSTANTIATE_TEST_SUITE_P(Native, ArdaCudaGpu, testing::Values(
        "d3d12-context", "vulkan-context", "d3d12-cig", "vulkan-cig"));
}
