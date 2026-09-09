#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaTestComputeOperand.h"
#include "Compute/ArdaCudaCompiler.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <future>
#include <cstdlib>
#include <iterator>

namespace
{
    using namespace arda;

    const char* AddPtx = GetArdaTestAddPtx();

    class FCudaDiagnostics : public IArdaDiagnosticCallback
    {
    public:
        std::atomic<uint32_t> mErrors{0};
        void Message(EArdaDiagnosticSeverity Severity, const char* Message) override
        {
            if (Severity == EArdaDiagnosticSeverity::Error || Severity == EArdaDiagnosticSeverity::Fatal)
            { ++mErrors; std::fprintf(stderr, "CUDA test validation: %s\n", Message); }
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
            C.mBackendName = GetParam();
            if (C.mBackendName == "d3d12-context" || C.mBackendName == "vulkan-context")
            {
                C.mBackendName = C.mBackendName == "d3d12-context" ? "native-d3d12" : "native-vulkan";
                C.mCudaExecutionMode = EArdaCudaExecutionMode::ContextSwitch;
            }
            else if (C.mBackendName == "d3d12-cig")
            {
                C.mBackendName = "native-d3d12";
                C.mCudaExecutionMode = EArdaCudaExecutionMode::GraphicsQueue;
            }
            if (!FindBackendModule(C.mBackendName.c_str())) GTEST_SKIP() << "Backend not built";
            C.mbEnableValidation = true;
            C.mMessageCallback = &mDiagnostics;
            C.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
            ASSERT_TRUE(ConfigureBackend(C));
            ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
            mDevice = GetDevice();
        }
        void TearDown() override
        {
            if (mDevice) EXPECT_TRUE(mDevice->WaitForIdle());
            mDevice.Reset(); ShutdownBackend();
            if (GetBackendInitializeResult() != EArdaInitializeResult::ValidationUnavailable)
                EXPECT_EQ(mDiagnostics.mErrors.load(), 0u);
        }
        FArdaRHIBufferDesc BufferDesc(uint64_t Size, bool Cuda = true)
        {
            FArdaRHIBufferDesc D;
            D.mByteSize = Size; D.mbCudaInterop = Cuda;
            D.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
            return D;
        }
    };

    eastl::string ReadArdaCudaOperandSource(const char* Name)
    {
        const auto Path = std::filesystem::path(ARDA_BACKEND_TEST_SHADER_SOURCE_DIR) / Name;
        std::ifstream File(Path, std::ios::binary);
        EXPECT_TRUE(File.is_open());
        const std::string Text{std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
        return {Text.data(), Text.size()};
    }


    TEST_P(ArdaCudaGpu, DefaultDeferredHookNeverExecutesImmediateWork)
    {
        class FImmediateOnly final : public TArdaComputeOperand<FArdaAddParameters>
        {
        public:
            uint32_t mCalls = 0;
            const char* GetName() const noexcept override { return "immediate-only"; }
            FArdaRHIStatus GetOperandSupport() const override { return {}; }
            FArdaRHIStatus Dispatch(const FArdaAddParameters&) override { ++mCalls; return {}; }
        } Operand;
        auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands); ASSERT_TRUE(Commands.mValue->Open());
        TArdaComputeOperand<FArdaAddParameters>& Base = Operand;
        EXPECT_EQ(Base.DispatchDeferred(*Commands.mValue, {}).mCode, EArdaRHIResult::Unsupported);
        EXPECT_EQ(Operand.mCalls, 0u);
        ASSERT_TRUE(Base.Dispatch({})); EXPECT_EQ(Operand.mCalls, 1u);
        ASSERT_TRUE(Commands.mValue->Close());
    }

    TEST_P(ArdaCudaGpu, TypedDispatchOwnsPolicyAndMultipleCudaSourceModules)
    {
        const auto Capabilities = mDevice->GetCudaCapabilities();
        if (!Capabilities) GTEST_SKIP() << Capabilities.mUnavailableReason.c_str();
        const char* LibraryPath = std::getenv("ARDA_TEST_NVRTC_LIBRARY");
        if (!LibraryPath) GTEST_SKIP() << "Set ARDA_TEST_NVRTC_LIBRARY to exercise CUDA C++ compilation.";
        auto Compiler = CreateArdaNvrtcCompiler(LibraryPath);
        ASSERT_TRUE(Compiler) << Compiler.mStatus.mMessage.c_str();
        uint32_t Compilations = 0;
        eastl::vector<eastl::shared_ptr<const FArdaCudaModule>> Modules;
        for (const char* Name : {"ArdaCudaOperand.cu", "ArdaCudaOperandFinish.cu"})
        {
            auto Module = FArdaCudaModule::Create({Name, ReadArdaCudaOperandSource(Name), EArdaCudaSourceLanguage::CudaCpp,
                {{"ArdaCudaOperandValue.h", ReadArdaCudaOperandSource("ArdaCudaOperandValue.h")},
                 {"ArdaCudaOperandMath.cuh", ReadArdaCudaOperandSource("ArdaCudaOperandMath.cuh")}}, {"--std=c++17"}},
                [&, Compile = Compiler.mValue](const auto& Source, uint32_t Architecture)
                { ++Compilations; return Compile(Source, Architecture); });
            ASSERT_TRUE(Module);
            Modules.push_back(Module.mValue);
        }
        FArdaAddOperand Operand(mDevice, {}, Modules);
        ASSERT_TRUE(Operand.GetOperandSupport());
        auto Source = mDevice->CreateBuffer(BufferDesc(128 * 4));
        auto Output = mDevice->CreateBuffer(BufferDesc(128 * 4));
        ASSERT_TRUE(Source); ASSERT_TRUE(Output);
        FArdaAddParameters P;
        P.mCount = 128; P.mInput.mBuffer = Source.mValue; P.mOutput.mBuffer = Output.mValue;
        for (const uint64_t Columns : {32u, 128u})
        {
            auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
            ASSERT_TRUE(Commands); ASSERT_TRUE(Commands.mValue->Open());
            eastl::vector<uint32_t> Values(128);
            for (uint32_t Index = 0; Index < Values.size(); ++Index) Values[Index] = Index * 3;
            ASSERT_TRUE(Commands.mValue->WriteBuffer(*Source.mValue, Values.data(), Values.size() * 4));
            P.mInputShape = P.mOutputShape = {128 / Columns, Columns};
            const auto Status = Operand.DispatchDeferred(*Commands.mValue, P);
            ASSERT_TRUE(Status) << Status.mMessage.c_str();
            EXPECT_EQ(Operand.mLastBlockSize, Columns == 32 ? 32u : 128u);
            EXPECT_EQ(Compilations, 2u);
            eastl::vector<uint8_t> Bytes;
            ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Output.mValue, Bytes));
            ASSERT_TRUE(Commands.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
            ASSERT_TRUE(mDevice->WaitForIdle());
            ASSERT_EQ(Bytes.size(), Values.size() * 4);
            for (uint32_t Index = 0; Index < Values.size(); ++Index)
            { uint32_t Value = 0; std::memcpy(&Value, Bytes.data() + Index * 4, 4); ASSERT_EQ(Value, Values[Index] + 18); }
        }
        // The same typed virtual interface also permits an implementation to submit now.
        TArdaComputeOperand<FArdaAddParameters>& Base = Operand;
        P.mInput.mBuffer = Output.mValue;
        ASSERT_TRUE(Base.Dispatch(P));
        EXPECT_GT(Operand.mLastSubmission, 0u);
        auto Read = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics); ASSERT_TRUE(Read);
        ASSERT_TRUE(Read.mValue->Open());
        eastl::vector<uint8_t> Bytes;
        ASSERT_TRUE(Read.mValue->CopyBufferDeviceToHost(*Output.mValue, Bytes));
        ASSERT_TRUE(Read.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Read.mValue));
        ASSERT_EQ(Bytes.size(), 128u * 4);
        for (uint32_t I = 0; I < 128; ++I)
        { uint32_t V; std::memcpy(&V, Bytes.data() + I * 4, 4); ASSERT_EQ(V, I * 3 + 36); }
    }

    TEST_P(ArdaCudaGpu, UserDispatchRejectsInvalidInputsAndPropagatesCompilerFailure)
    {
        uint32_t Compilations = 0, GraphicsCalls = 0;
        auto Module = FArdaCudaModule::Create({"ArdaUnavailable.cu", "invalid CUDA C++"},
            [&](const auto&, uint32_t) -> TArdaRHIResult<eastl::string>
            { ++Compilations; return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "compile failed")}; });
        ASSERT_TRUE(Module);
        FArdaAddOperand Operand(mDevice, [&](auto&, const auto&) { ++GraphicsCalls; return FArdaRHIStatus{}; },
            {Module.mValue, Module.mValue});
        auto Buffer = mDevice->CreateBuffer(BufferDesc(512, false)); ASSERT_TRUE(Buffer);
        auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(Commands); ASSERT_TRUE(Commands.mValue->Open());
        FArdaAddParameters P;
        P.mCount = 128; P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
        P.mInputShape = P.mOutputShape = {1, 128};
        ASSERT_TRUE(Operand.DispatchDeferred(*Commands.mValue, P));
        EXPECT_EQ(Operand.mLastImplementation, "graphics.add");
        FArdaAddOperand CudaOnly(mDevice, {}, {Module.mValue, Module.mValue});
        EXPECT_EQ(CudaOnly.DispatchDeferred(*Commands.mValue, P).mCode, EArdaRHIResult::Unsupported);
        P.mInputShape = {1, 0}; EXPECT_FALSE(Operand.DispatchDeferred(*Commands.mValue, P));
        P.mInputShape = {128}; EXPECT_FALSE(Operand.DispatchDeferred(*Commands.mValue, P));
        P.mInputShape = {2, 64}; EXPECT_FALSE(Operand.DispatchDeferred(*Commands.mValue, P));
        P.mInputShape = P.mOutputShape = {UINT64_MAX, UINT64_MAX};
        EXPECT_FALSE(Operand.DispatchDeferred(*Commands.mValue, P));
        EXPECT_EQ(GraphicsCalls, 1u); EXPECT_EQ(Compilations, 0u);
        if (mDevice->GetCudaCapabilities())
        {
            auto Shared = mDevice->CreateBuffer(BufferDesc(512)); ASSERT_TRUE(Shared);
            P.mInput.mBuffer = P.mOutput.mBuffer = Shared.mValue;
            P.mInputShape = P.mOutputShape = {1, 128};
            Operand.mbPreferGraphics = true;
            ASSERT_TRUE(Operand.DispatchDeferred(*Commands.mValue, P));
            EXPECT_EQ(Compilations, 0u);
            Operand.mbPreferGraphics = false;
            const auto Failed = Operand.DispatchDeferred(*Commands.mValue, P);
            EXPECT_EQ(Failed.mCode, EArdaRHIResult::BackendFailure); EXPECT_EQ(Failed.mMessage, "compile failed");
            EXPECT_EQ(Compilations, 1u); EXPECT_EQ(GraphicsCalls, 2u);
        }
        ASSERT_TRUE(Commands.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
    }

    TEST_P(ArdaCudaGpu, CapabilityAndResourceRejections)
    {
        const auto C = mDevice->GetCudaCapabilities();
        std::printf("%s CUDA mode=%u SM=%u threads=%u shared=%u reason=%s\n", GetParam(),
            unsigned(C.mLaunchMode), C.mComputeCapability, C.mMaxThreadsPerBlock, C.mMaxSharedMemoryBytes, C.mUnavailableReason.c_str());
        auto Normal = mDevice->CreateBuffer(BufferDesc(256, false));
        ASSERT_TRUE(Normal);
        EXPECT_FALSE(Normal.mValue->GetCudaResourceInfo().mbSharingEnabled);
        auto D = BufferDesc(256); D.mbVirtual = true;
        EXPECT_FALSE(mDevice->CreateBuffer(D));
        D = BufferDesc(256); D.mUsage |= EArdaRHIBufferUsage::AccelStructStorage;
        EXPECT_FALSE(mDevice->CreateBuffer(D));
        D = BufferDesc(256); D.mCpuAccess = EArdaRHICpuAccess::Read;
        EXPECT_FALSE(mDevice->CreateBuffer(D));
        D = BufferDesc(256); D.mMaxVersions = 2;
        EXPECT_FALSE(mDevice->CreateBuffer(D));
        FArdaRHITextureDesc T; T.mbCudaInterop = true; T.mFormat = EArdaRHIFormat::D32;
        EXPECT_FALSE(mDevice->CreateTexture(T));
        T.mFormat = EArdaRHIFormat::SRGBA8UNorm;
        EXPECT_FALSE(mDevice->CreateTexture(T));
        if (!C.mbSurfaceAccess)
        {
            T.mFormat = EArdaRHIFormat::R32UInt;
            EXPECT_FALSE(mDevice->CreateTexture(T));
            EXPECT_FALSE(C.mSurfaceUnavailableReason.empty());
        }
        if (!C) EXPECT_FALSE(mDevice->CreateBuffer(BufferDesc(256)));
    }

    TEST_P(ArdaCudaGpu, OrderedKernelsShareBufferAndPreserveGuardBytes)
    {
        const auto C = mDevice->GetCudaCapabilities();
        if (!C) GTEST_SKIP() << C.mUnavailableReason.c_str();
        constexpr uint32_t Count = 1027;
        eastl::vector<uint32_t> Input(Count), Output(Count + 8, 0xdeadbeefu);
        for (uint32_t I = 0; I < Count; ++I) Input[I] = I * 3;
        auto Src = mDevice->CreateBuffer(BufferDesc(Input.size() * 4));
        auto Dst = mDevice->CreateBuffer(BufferDesc(Output.size() * 4));
        ASSERT_TRUE(Src) << Src.mStatus.mMessage.c_str(); ASSERT_TRUE(Dst) << Dst.mStatus.mMessage.c_str();
        EXPECT_EQ(Dst.mValue->GetCudaResourceInfo().mRepresentations, EArdaResourceRepresentations::GraphicsAndCuda);
        auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics); ASSERT_TRUE(Cmd);
        ASSERT_TRUE(Cmd.mValue->Open());
        ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Src.mValue, Input.data(), Input.size()*4, 0));
        ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Dst.mValue, Output.data(), Output.size()*4, 0));
        FArdaAddParameters I;
        I.mCount = Count;
        I.mInput.mBuffer = Src.mValue; I.mOutput.mBuffer = Dst.mValue;
        I.mOutput.mRange = {16, Count * 4};
        auto Dispatched = FArdaAddOperand(mDevice).DispatchDeferred(*Cmd.mValue, I);
        ASSERT_TRUE(Dispatched) << Dispatched.mMessage.c_str();
        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Dst.mValue, Readback));
        ASSERT_TRUE(Cmd.mValue->Close());
        auto Submitted = mDevice->ExecuteCommandList(Cmd.mValue); ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
        ASSERT_TRUE(mDevice->WaitForIdle());
        ASSERT_EQ(Readback.size(), Output.size()*4);
        std::memcpy(Output.data(), Readback.data(), Readback.size());
        for (uint32_t J = 0; J < Count; ++J) ASSERT_EQ(Output[J+4], Input[J]+18) << "element " << J;
        for (uint32_t J : {0u,1u,2u,3u,Count+4,Count+5,Count+6,Count+7}) EXPECT_EQ(Output[J], 0xdeadbeefu);
        if (C.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG || C.mLaunchMode == EArdaCudaLaunchMode::ContextSwitch)
            EXPECT_FALSE(mDevice->ExecuteCommandList(Cmd.mValue));
    }

    TEST_P(ArdaCudaGpu, SurfaceWritesReachGraphicsReadbackAtSelectedMip)
    {
        const auto C = mDevice->GetCudaCapabilities();
        if (!C || !C.mbSurfaceAccess) GTEST_SKIP() << C.mSurfaceUnavailableReason.c_str();
        const char* Ptx = R"ptx(.version 8.0
.target sm_70
.address_size 64
.visible .entry fill_surface(.param .u64 surface, .param .u32 width, .param .u32 height)
{
 .reg .pred p; .reg .b32 r<8>; .reg .b64 s;
 ld.param.u64 s,[surface]; ld.param.u32 r0,[width]; ld.param.u32 r1,[height];
 mov.u32 r2,%ctaid.x; mov.u32 r3,%ntid.x; mov.u32 r4,%tid.x; mad.lo.u32 r2,r2,r3,r4;
 mov.u32 r3,%ctaid.y; mov.u32 r4,%ntid.y; mov.u32 r5,%tid.y; mad.lo.u32 r3,r3,r4,r5;
 setp.ge.u32 p,r2,r0; @p bra done; setp.ge.u32 p,r3,r1; @p bra done;
 mad.lo.u32 r6,r3,r0,r2; add.u32 r6,r6,101; shl.b32 r7,r2,2;
 sust.b.2d.b32.trap [s,{r7,r3}],r6;
done: ret;
})ptx";
        FArdaRHITextureDesc D;
        D.mbCudaInterop = true; D.mWidth = 54; D.mHeight = 38; D.mMipLevels = 2;
        D.mFormat = EArdaRHIFormat::R32UInt;
        D.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
        auto Texture = mDevice->CreateTexture(D); ASSERT_TRUE(Texture) << Texture.mStatus.mMessage.c_str();
        EXPECT_EQ(Texture.mValue->GetCudaResourceInfo().mSupportedRepresentation, EArdaCudaRepresentation::Surface);
        FArdaRHIStagingTextureDesc Staging;
        Staging.mTexture = D; Staging.mTexture.mbCudaInterop = false; Staging.mCpuAccess = EArdaRHICpuAccess::Read;
        auto Readback = mDevice->CreateStagingTexture(Staging); ASSERT_TRUE(Readback);
        auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics); ASSERT_TRUE(Cmd); ASSERT_TRUE(Cmd.mValue->Open());
        FArdaCudaKernel K;
        K.mPtx = Ptx; K.mEntryPoint = "fill_surface";
        K.mBlockSize[0] = K.mBlockSize[1] = 8;
        K.mGridSize[0] = 4; K.mGridSize[1] = 3;
        K.mArguments = {FArdaCudaArgument::Binding(0), FArdaCudaArgument::Value(27u), FArdaCudaArgument::Value(19u)};
        FArdaCudaBinding B; B.mResource = Texture.mValue; B.mMipLevel = 1; B.mAccess = EArdaComputeAccess::Write;
        const auto Dispatched = Cmd.mValue->DispatchCuda({{B}, {K}});
        ASSERT_TRUE(Dispatched) << Dispatched.mMessage.c_str();
        FArdaRHITextureSlice Slice; Slice.mMipLevel = 1;
        ASSERT_TRUE(Cmd.mValue->CopyTextureToStaging(*Readback.mValue, Slice, *Texture.mValue, Slice));
        ASSERT_TRUE(Cmd.mValue->Close());
        auto Submitted = mDevice->ExecuteCommandList(Cmd.mValue); ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
        // The submission must retain the native image, CUDA mapping and executable.
        B.mResource.Reset();
        Texture.mValue.Reset(); Cmd.mValue.Reset();
        ASSERT_TRUE(mDevice->WaitForIdle());
        auto Mapped = mDevice->MapStagingTexture(Readback.mValue, Slice, EArdaRHICpuAccess::Read); ASSERT_TRUE(Mapped);
        for (uint32_t Y = 0; Y < 19; ++Y)
        {
            const auto* Row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(Mapped.mValue.mData) + Y * Mapped.mValue.mRowPitch);
            for (uint32_t X = 0; X < 27; ++X) EXPECT_EQ(Row[X], Y*27+X+101) << X << "," << Y;
        }
        mDevice->UnmapStagingTexture(Readback.mValue);
    }
    TEST_P(ArdaCudaGpu, TextureFormatDimensionAndReadWriteMatrix)
    {
        if (!mDevice->GetCudaCapabilities().mbSurfaceAccess)
            GTEST_SKIP() << mDevice->GetCudaCapabilities().mSurfaceUnavailableReason.c_str();
        for (uint32_t Width : {54u, 64u, 256u})
            for (uint32_t Mips : {1u, 2u})
                for (bool RenderTarget : {false, true})
                {
                    FArdaRHITextureDesc D;
                    D.mbCudaInterop = true; D.mWidth = Width; D.mHeight = Width; D.mMipLevels = Mips;
                    D.mFormat = EArdaRHIFormat::R32UInt;
                    D.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
                    if (RenderTarget) D.mUsage |= EArdaRHITextureUsage::RenderTarget;
                    auto T = mDevice->CreateTexture(D);
                    EXPECT_TRUE(T) << "width=" << Width << " mips=" << Mips << " rt=" << RenderTarget << " " << T.mStatus.mMessage.c_str();
                }
        for (uint32_t F = 1; F < uint32_t(EArdaRHIFormat::Count); ++F)
        {
            const auto Format = static_cast<EArdaRHIFormat>(F);
            if (!GetArdaCudaFormatInfo(Format).mChannels) continue;
            for (auto Dimension : {EArdaRHITextureDimension::Texture1D, EArdaRHITextureDimension::Texture1DArray,
                EArdaRHITextureDimension::Texture2D, EArdaRHITextureDimension::Texture2DArray, EArdaRHITextureDimension::Texture3D})
            {
                FArdaRHITextureDesc D;
                D.mbCudaInterop = true; D.mDimension = Dimension; D.mWidth = 32; D.mMipLevels = 2;
                D.mHeight = Dimension == EArdaRHITextureDimension::Texture1D || Dimension == EArdaRHITextureDimension::Texture1DArray ? 1 : 16;
                D.mDepth = Dimension == EArdaRHITextureDimension::Texture3D ? 4 : 1;
                D.mArraySize = Dimension == EArdaRHITextureDimension::Texture1DArray || Dimension == EArdaRHITextureDimension::Texture2DArray ? 3 : 1;
                D.mFormat = Format; D.mUsage = EArdaRHITextureUsage::UnorderedAccess;
                auto T = mDevice->CreateTexture(D);
                if ((Dimension == EArdaRHITextureDimension::Texture1DArray || Dimension == EArdaRHITextureDimension::Texture2DArray) &&
                    !mDevice->GetCudaCapabilities().mbLayeredSurfaceAccess)
                {
                    EXPECT_FALSE(T);
                    EXPECT_EQ(T.mStatus.mCode, EArdaRHIResult::Unsupported);
                    continue;
                }
                ASSERT_TRUE(T) << "format=" << F << " dimension=" << uint32_t(Dimension) << " " << T.mStatus.mMessage.c_str();
                SCOPED_TRACE(testing::Message() << "format=" << F << " dimension=" << uint32_t(Dimension));
                const char* Geometry = "2d"; const char* Coordinates = "{x,y}";
                switch (Dimension)
                {
                case EArdaRHITextureDimension::Texture1D: Geometry = "1d"; Coordinates = "{x}"; break;
                case EArdaRHITextureDimension::Texture1DArray: Geometry = "a1d"; Coordinates = "{z,x}"; break;
                case EArdaRHITextureDimension::Texture2DArray: Geometry = "a2d"; Coordinates = "{z,x,y,0}"; break;
                case EArdaRHITextureDimension::Texture3D: Geometry = "3d"; Coordinates = "{x,y,z,0}"; break;
                default: break;
                }
                FArdaCudaKernel K;
                K.mPtx = ".version 8.0\n.target sm_70\n.address_size 64\n.visible .entry fill_bytes(.param .u64 surface) {\n"
                    ".reg .b64 s; .reg .b32 x,y,z,t,v; ld.param.u64 s,[surface];\n"
                    "mov.u32 x,%ctaid.x; mov.u32 t,%tid.x; mad.lo.u32 x,x,8,t;\n"
                    "mov.u32 y,%ctaid.y; mov.u32 z,%ctaid.z; mov.u32 v,90;\nsust.b.";
                K.mPtx += Geometry; K.mPtx += ".b8.trap [s,"; K.mPtx += Coordinates; K.mPtx += "],v; ret; }";
                const auto FormatInfo = GetArdaCudaFormatInfo(Format);
                const uint32_t RowBytes = 16 * FormatInfo.mBits * FormatInfo.mChannels / 8;
                const uint32_t Height = D.mHeight > 1 ? D.mHeight / 2 : 1;
                const uint32_t Depth = D.mDepth > 1 ? D.mDepth / 2 : 1;
                K.mEntryPoint = "fill_bytes"; K.mBlockSize[0] = 8;
                K.mGridSize[0] = RowBytes / 8; K.mGridSize[1] = Height; K.mGridSize[2] = Depth * D.mArraySize;
                K.mArguments = {FArdaCudaArgument::Binding(0)};
                auto CudaReadback = mDevice->CreateBuffer(BufferDesc(uint64_t(RowBytes) * Height * Depth * D.mArraySize));
                ASSERT_TRUE(CudaReadback);
                auto ReadKernel = K;
                ReadKernel.mEntryPoint = "read_bytes";
                ReadKernel.mPtx = ".version 8.0\n.target sm_70\n.address_size 64\n.visible .entry read_bytes("
                    ".param .u64 surface, .param .u64 output, .param .u32 row_bytes, .param .u32 height) {\n"
                    ".reg .b64 s,a,b; .reg .b32 x,y,z,t,v,w,h,i; ld.param.u64 s,[surface]; ld.param.u64 a,[output];\n"
                    "ld.param.u32 w,[row_bytes]; ld.param.u32 h,[height];\n"
                    "mov.u32 x,%ctaid.x; mov.u32 t,%tid.x; mad.lo.u32 x,x,8,t;\n"
                    "mov.u32 y,%ctaid.y; mov.u32 z,%ctaid.z;\nsuld.b.";
                ReadKernel.mPtx += Geometry; ReadKernel.mPtx += ".b8.trap v,[s,"; ReadKernel.mPtx += Coordinates;
                ReadKernel.mPtx += "]; mad.lo.u32 i,z,h,y; mad.lo.u32 i,i,w,x; cvt.u64.u32 b,i; add.u64 a,a,b; st.global.u8 [a],v; ret; }";
                ReadKernel.mArguments = {FArdaCudaArgument::Binding(0), FArdaCudaArgument::Binding(1),
                    FArdaCudaArgument::Value(RowBytes), FArdaCudaArgument::Value(Height)};
                FArdaRHIStagingTextureDesc Staging;
                Staging.mTexture = D; Staging.mTexture.mbCudaInterop = false; Staging.mCpuAccess = EArdaRHICpuAccess::Read;
                auto Readback = mDevice->CreateStagingTexture(Staging); ASSERT_TRUE(Readback);
                auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics); ASSERT_TRUE(Cmd); ASSERT_TRUE(Cmd.mValue->Open());
                FArdaCudaBinding Binding; Binding.mResource = T.mValue; Binding.mMipLevel = 1; Binding.mAccess = EArdaComputeAccess::ReadWrite;
                FArdaCudaBinding Output; Output.mResource = CudaReadback.mValue; Output.mAccess = EArdaComputeAccess::Write;
                auto Status = Cmd.mValue->DispatchCuda({{Binding, Output}, {K, ReadKernel}}); ASSERT_TRUE(Status) << Status.mMessage.c_str();
                eastl::vector<uint8_t> CudaBytes;
                ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*CudaReadback.mValue, CudaBytes));
                for (uint32_t Layer = 0; Layer < D.mArraySize; ++Layer)
                {
                    FArdaRHITextureSlice Slice; Slice.mMipLevel = 1; Slice.mArraySlice = Layer;
                    ASSERT_TRUE(Cmd.mValue->CopyTextureToStaging(*Readback.mValue, Slice, *T.mValue, Slice));
                }
                ASSERT_TRUE(Cmd.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue)); ASSERT_TRUE(mDevice->WaitForIdle());
                ASSERT_EQ(CudaBytes.size(), uint64_t(RowBytes) * Height * Depth * D.mArraySize);
                for (auto Byte : CudaBytes) ASSERT_EQ(Byte, 90u);
                for (uint32_t Layer = 0; Layer < D.mArraySize; ++Layer)
                {
                    SCOPED_TRACE(testing::Message() << "layer=" << Layer);
                    FArdaRHITextureSlice Slice; Slice.mMipLevel = 1; Slice.mArraySlice = Layer;
                    auto M = mDevice->MapStagingTexture(Readback.mValue, Slice, EArdaRHICpuAccess::Read); ASSERT_TRUE(M);
                    for (uint32_t Z = 0; Z < Depth; ++Z)
                        for (uint32_t Y = 0; Y < Height; ++Y)
                        {
                            const auto* Row = static_cast<const uint8_t*>(M.mValue.mData) + Z*M.mValue.mDepthPitch + Y*M.mValue.mRowPitch;
                            for (uint32_t X = 0; X < RowBytes; ++X) EXPECT_EQ(Row[X], 90u) << X << "," << Y << "," << Z;
                        }
                    mDevice->UnmapStagingTexture(Readback.mValue);
                }
            }
        }
    }
    TEST_P(ArdaCudaGpu, GraphicsFallbackAndMixedShaderCudaSequence)
    {
        const auto Path = std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) /
            (std::string("ArdaCudaFallback") + GetShaderArtifactExtension(GetBackendConfiguration().mBackendName.c_str()));
        std::ifstream File(Path, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(File);
        eastl::vector<uint8_t> Bytes(static_cast<size_t>(File.tellg()));
        File.seekg(0); File.read(reinterpret_cast<char*>(Bytes.data()), Bytes.size());
        FArdaRHIShaderDesc SD;
        SD.mStage = EArdaRHIShaderStage::Compute; SD.mEntryPoint = "AddFallbackCS";
        SD.mBytecode = Bytes.data(); SD.mBytecodeSize = Bytes.size();
        auto Shader = mDevice->CreateShader(SD); ASSERT_TRUE(Shader);
        FArdaRHIBindingLayoutDesc LD;
        LD.mVisibility = EArdaRHIShaderStage::Compute;
        LD.mItems = {{0, 1, EArdaRHIBindingType::StructuredBufferSRV}, {0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
        auto Layout = mDevice->CreateBindingLayout(LD); ASSERT_TRUE(Layout);
        FArdaRHIComputePipelineDesc PD;
        PD.mComputeShader = Shader.mValue; PD.mBindingLayouts = {Layout.mValue};
        auto Pipeline = mDevice->CreateComputePipeline(PD); ASSERT_TRUE(Pipeline);
        FArdaAddOperand Operand(mDevice, [&](IArdaRHICommandList& Commands, const FArdaAddParameters& I)
        {
            FArdaRHIBindingSetDesc BD; BD.mLayout = Layout.mValue;
            for (uint32_t J = 0; J < 2; ++J)
            {
                auto* Buffer = (J ? I.mOutput.mBuffer.Get() : I.mInput.mBuffer.Get());
                if (auto S = Commands.SetBufferState(*Buffer, J ? EArdaRHIResourceState::UnorderedAccess :
                    EArdaRHIResourceState::ShaderResource); !S) return S;
                FArdaRHIBindingItem Item;
                Item.mType = J ? EArdaRHIBindingType::StructuredBufferUAV : EArdaRHIBindingType::StructuredBufferSRV;
                Item.mResource = J ? I.mOutput.mBuffer : I.mInput.mBuffer;
                Item.mView.mBufferRange = J ? I.mOutput.mRange : I.mInput.mRange;
                BD.mItems.push_back(Item);
            }
            auto Set = mDevice->CreateBindingSet(BD); if (!Set) return Set.mStatus;
            FArdaRHIComputeState State; State.mPipeline = Pipeline.mValue; State.mBindings = {Set.mValue};
            if (auto S = Commands.SetComputeState(State); !S) return S;
            Commands.Dispatch((I.mCount + 127) / 128, 1, 1);
            return FArdaRHIStatus{};
        });
        for (bool Share : {false, true})
        {
            if (Share && !mDevice->GetCudaCapabilities()) continue;
            constexpr uint32_t Count = 259;
            auto D = BufferDesc(Count * 4, Share);
            D.mStructureStride = 4; D.mUsage |= EArdaRHIBufferUsage::Structured;
            auto A = mDevice->CreateBuffer(D), B = mDevice->CreateBuffer(D); ASSERT_TRUE(A); ASSERT_TRUE(B);
            eastl::vector<uint32_t> Values(Count);
            for (uint32_t J = 0; J < Count; ++J) Values[J] = J * 5;
            auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics); ASSERT_TRUE(Cmd); ASSERT_TRUE(Cmd.mValue->Open());
            ASSERT_TRUE(Cmd.mValue->WriteBuffer(*A.mValue, Values.data(), D.mByteSize, 0));
            FArdaAddParameters I; I.mCount = Count; I.mInput.mBuffer = A.mValue; I.mOutput.mBuffer = B.mValue;
            Operand.mbPreferGraphics = true;
            auto First = Operand.DispatchDeferred(*Cmd.mValue, I);
            ASSERT_TRUE(First) << First.mMessage.c_str(); EXPECT_EQ(Operand.mLastImplementation, "graphics.add");
            I.mInput.mBuffer = B.mValue; I.mOutput.mBuffer = A.mValue;
            Operand.mbPreferGraphics = false;
            auto Middle = Operand.DispatchDeferred(*Cmd.mValue, I);
            ASSERT_TRUE(Middle) << Middle.mMessage.c_str();
            EXPECT_EQ(Operand.mLastImplementation, Share ? "cuda.sequence" : "graphics.add");
            I.mInput.mBuffer = A.mValue; I.mOutput.mBuffer = B.mValue;
            Operand.mbPreferGraphics = true;
            ASSERT_TRUE(Operand.DispatchDeferred(*Cmd.mValue, I));
            eastl::vector<uint8_t> Result;
            ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*B.mValue, Result));
            ASSERT_TRUE(Cmd.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
            ASSERT_EQ(Result.size(), D.mByteSize);
            for (uint32_t J = 0; J < Count; ++J)
            {
                uint32_t Value; std::memcpy(&Value, Result.data() + J*4, 4);
                ASSERT_EQ(Value, Values[J] + 54) << J;
            }
        }
    }

    TEST_P(ArdaCudaGpu, RejectsInvalidContractsBeforeLaunching)
    {
        const bool Cuda = bool(mDevice->GetCudaCapabilities());
        auto B = mDevice->CreateBuffer(BufferDesc(256, Cuda)); ASSERT_TRUE(B);
        auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics); ASSERT_TRUE(Cmd); ASSERT_TRUE(Cmd.mValue->Open());
        FArdaAddParameters I; I.mCount = 64; I.mInput.mBuffer = I.mOutput.mBuffer = B.mValue;
        FArdaAddOperand Operand(mDevice);
        I.mCount = 65; EXPECT_FALSE(Operand.DispatchDeferred(*Cmd.mValue, I)); I.mCount = 64;
        Operand.mbPreferGraphics = true; EXPECT_FALSE(Operand.DispatchDeferred(*Cmd.mValue, I)); Operand.mbPreferGraphics = false;
        I.mInput.mRange = {4, 256}; EXPECT_FALSE(Operand.DispatchDeferred(*Cmd.mValue, I));
        I.mInput.mRange = {2, 252}; EXPECT_FALSE(Operand.DispatchDeferred(*Cmd.mValue, I));
        I.mInput.mRange = {}; I.mOutput.mBuffer.Reset(); EXPECT_FALSE(Operand.DispatchDeferred(*Cmd.mValue, I));
        FArdaCudaKernel K; K.mPtx = AddPtx; K.mEntryPoint = "add_values";
        K.mArguments = {FArdaCudaArgument::Binding(0), FArdaCudaArgument::Binding(0),
            FArdaCudaArgument::Value(64u), FArdaCudaArgument::Value(1u)};
        auto InvalidKernel = K; InvalidKernel.mBlockSize[0] = UINT32_MAX;
        EXPECT_FALSE(Cmd.mValue->DispatchCuda({{{B.mValue}}, {InvalidKernel}}));
        InvalidKernel = K; InvalidKernel.mArguments[0] = FArdaCudaArgument::Binding(1);
        EXPECT_FALSE(Cmd.mValue->DispatchCuda({{{B.mValue}}, {InvalidKernel}}));
        EXPECT_FALSE(Cmd.mValue->DispatchCuda({{{B.mValue}}, {}}));
        auto Closed = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics); ASSERT_TRUE(Closed);
        EXPECT_FALSE(Closed.mValue->DispatchCuda({{{B.mValue}}, {K}}));
        if (mDevice->GetCudaCapabilities().mLaunchMode == EArdaCudaLaunchMode::D3D12CiG)
        {
            InvalidKernel = K; InvalidKernel.mArguments.pop_back();
            EXPECT_FALSE(Cmd.mValue->DispatchCuda({{{B.mValue}}, {InvalidKernel}}));
        }
        ASSERT_TRUE(Cmd.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
    }

    TEST_P(ArdaCudaGpu, CiGCaptureOrderAndDiscard)
    {
        if (mDevice->GetCudaCapabilities().mLaunchMode != EArdaCudaLaunchMode::D3D12CiG) GTEST_SKIP();
        FArdaCudaKernel K;
        K.mPtx = ".version 8.0\n.target sm_70\n.address_size 64\n.visible .entry empty_kernel() { ret; }";
        K.mEntryPoint = "empty_kernel";
        auto First = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        auto Next = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(First); ASSERT_TRUE(Next);
        ASSERT_TRUE(First.mValue->Open()); ASSERT_TRUE(Next.mValue->Open());
        ASSERT_TRUE(First.mValue->DispatchCuda({{}, {K}}));
        EXPECT_FALSE(Next.mValue->DispatchCuda({{}, {K}}));
        // Discarding an unsubmitted capture releases the ordering reservation.
        First.mValue.Reset();
        ASSERT_TRUE(Next.mValue->DispatchCuda({{}, {K}}));
        ASSERT_TRUE(Next.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Next.mValue));
        EXPECT_FALSE(mDevice->ExecuteCommandList(Next.mValue));
        ASSERT_TRUE(mDevice->WaitForIdle());
    }

    TEST_P(ArdaCudaGpu, SubmittedBufferAndExecutableSurviveCallerRelease)
    {
        if (!mDevice->GetCudaCapabilities()) GTEST_SKIP() << mDevice->GetCudaCapabilities().mUnavailableReason.c_str();
        constexpr uint32_t Count = 262144;
        eastl::vector<uint32_t> Values(Count);
        for (uint32_t J = 0; J < Count; ++J) Values[J] = J * 3;
        auto Buffer = mDevice->CreateBuffer(BufferDesc(Count * 4)); ASSERT_TRUE(Buffer);
        auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics); ASSERT_TRUE(Cmd); ASSERT_TRUE(Cmd.mValue->Open());
        ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Count * 4, 0));
        FArdaAddParameters I; I.mCount = Count; I.mInput.mBuffer = I.mOutput.mBuffer = Buffer.mValue;
        ASSERT_TRUE(FArdaAddOperand(mDevice).DispatchDeferred(*Cmd.mValue, I));
        auto Promise = std::make_shared<std::promise<FArdaRHIBufferReadbackResult>>();
        auto Future = Promise->get_future();
        ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHostAsync(*Buffer.mValue,
            [Promise](FArdaRHIBufferReadbackResult Result) { Promise->set_value(eastl::move(Result)); }));
        ASSERT_TRUE(Cmd.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
        I.mInput.mBuffer.Reset(); I.mOutput.mBuffer.Reset(); Buffer.mValue.Reset(); Cmd.mValue.Reset();
        ASSERT_TRUE(mDevice->WaitForIdle());
        ASSERT_EQ(Future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
        auto Result = Future.get(); ASSERT_TRUE(Result); ASSERT_EQ(Result.mValue.size(), Count * 4);
        for (uint32_t J = 0; J < Count; ++J)
        {
            uint32_t Value; std::memcpy(&Value, Result.mValue.data() + J*4, 4);
            ASSERT_EQ(Value, Values[J]+18) << J;
        }
    }

    TEST_P(ArdaCudaGpu, ContextSwitchDefersDiscardedWorkAndAllowsIndependentRecordings)
    {
        if (mDevice->GetCudaCapabilities().mLaunchMode != EArdaCudaLaunchMode::ContextSwitch) GTEST_SKIP();
        auto Buffer = mDevice->CreateBuffer(BufferDesc(4)); ASSERT_TRUE(Buffer);
        auto Initial = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics); ASSERT_TRUE(Initial);
        ASSERT_TRUE(Initial.mValue->Open());
        const uint32_t Zero = 0;
        ASSERT_TRUE(Initial.mValue->WriteBuffer(*Buffer.mValue, &Zero, 4, 0));
        ASSERT_TRUE(Initial.mValue->SetBufferState(*Buffer.mValue, EArdaRHIResourceState::UnorderedAccess));
        ASSERT_TRUE(Initial.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Initial.mValue));
        ASSERT_TRUE(mDevice->WaitForIdle());
        auto First = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        auto Second = mDevice->CreateCommandList(EArdaRHIQueueType::Compute);
        auto Discarded = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        ASSERT_TRUE(First); ASSERT_TRUE(Second); ASSERT_TRUE(Discarded);
        const auto Record = [&](const FArdaRHICommandListRef& Commands, uint32_t Bias)
        {
            ASSERT_TRUE(Commands->Open());
            FArdaCudaKernel Kernel;
            Kernel.mPtx = AddPtx; Kernel.mEntryPoint = "add_values";
            Kernel.mArguments = {FArdaCudaArgument::Binding(0), FArdaCudaArgument::Binding(0),
                FArdaCudaArgument::Value(1u), FArdaCudaArgument::Value(Bias)};
            FArdaCudaBinding Binding; Binding.mResource = Buffer.mValue; Binding.mAccess = EArdaComputeAccess::ReadWrite;
            ASSERT_TRUE(Commands->DispatchCuda({{Binding}, {Kernel}}));
            ASSERT_TRUE(Commands->Close());
        };
        Record(First.mValue, 1); Record(Second.mValue, 2); Record(Discarded.mValue, 100);
        ASSERT_TRUE(Discarded.mValue->Reset()); ASSERT_TRUE(Discarded.mValue->Close());
        ASSERT_TRUE(mDevice->ExecuteCommandList(Discarded.mValue));
        ASSERT_TRUE(mDevice->ExecuteCommandList(First.mValue));
        ASSERT_TRUE(mDevice->ExecuteCommandList(Second.mValue));
        ASSERT_TRUE(mDevice->WaitForIdle());
        auto Read = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics); ASSERT_TRUE(Read);
        ASSERT_TRUE(Read.mValue->Open());
        eastl::vector<uint8_t> Bytes;
        ASSERT_TRUE(Read.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
        ASSERT_TRUE(Read.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Read.mValue));
        ASSERT_EQ(Bytes.size(), 4u);
        uint32_t Value = 0; std::memcpy(&Value, Bytes.data(), 4);
        EXPECT_EQ(Value, 3u);
    }

    INSTANTIATE_TEST_SUITE_P(Native, ArdaCudaGpu, testing::Values(
        "native-d3d12", "native-vulkan", "d3d12-context", "vulkan-context", "d3d12-cig"));
}
