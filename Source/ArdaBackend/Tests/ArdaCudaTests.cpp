#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "Compute/ArdaComputeOperand.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <future>

namespace
{
    using namespace arda::rhi;
    using namespace arda::backend;

    // PTX is the sample's source, not a second handwritten copy of a CUDA C kernel.
    const char* AddPtx = R"ptx(.version 8.0
.target sm_70
.address_size 64
.visible .entry add_values(.param .u64 src, .param .u64 dst, .param .u32 count, .param .u32 bias)
{
 .reg .pred p; .reg .b32 r<6>; .reg .b64 a<5>;
 ld.param.u64 a0,[src]; ld.param.u64 a1,[dst]; ld.param.u32 r0,[count]; ld.param.u32 r1,[bias];
 mov.u32 r2,%ctaid.x; mov.u32 r3,%ntid.x; mov.u32 r4,%tid.x;
 mad.lo.u32 r2,r2,r3,r4; setp.ge.u32 p,r2,r0; @p bra done;
 mul.wide.u32 a2,r2,4; add.u64 a3,a0,a2; add.u64 a4,a1,a2;
 ld.global.u32 r5,[a3]; add.u32 r5,r5,r1; st.global.u32 [a4],r5;
done: ret;
})ptx";

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
            if (!FindBackendModule(GetParam())) GTEST_SKIP() << "Backend not built";
            FArdaBackendConfiguration C;
            C.mBackendName = GetParam();
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

    class FAddOperand final : public FArdaComputeOperand
    {
    public:
        FAddOperand(FArdaComputeShaderDispatch Fallback = {}) : FArdaComputeOperand("sample.add_twice", {
            {"input", EArdaComputeBindingType::Buffer, EArdaComputeAccess::Read, 4, 4},
            {"output", EArdaComputeBindingType::Buffer, EArdaComputeAccess::ReadWrite, 4, 4}})
        {
            EXPECT_TRUE(RegisterCuda("cuda.sequence", 70, UINT32_MAX, [](const FArdaComputeInvocation& I)
                -> TArdaRHIResult<eastl::vector<FArdaCudaKernel>>
            {
                FArdaCudaKernel K;
                K.mPtx = AddPtx; K.mEntryPoint = "add_values";
                K.mBlockSize[0] = 128; K.mGridSize[0] = (I.mExtent[0] + 127) / 128;
                K.mArguments = {FArdaCudaArgument::Binding(0), FArdaCudaArgument::Binding(1),
                    FArdaCudaArgument::Value(I.mExtent[0]), FArdaCudaArgument::Value(7u)};
                auto Second = K;
                Second.mArguments[0] = FArdaCudaArgument::Binding(1);
                Second.mArguments[3] = FArdaCudaArgument::Value(11u);
                return {{K, Second}, {}};
            }));
            if (Fallback) EXPECT_TRUE(RegisterGraphics("graphics.add", eastl::move(Fallback)));
        }
        FArdaRHIStatus ValidateInvocation(const FArdaComputeInvocation& I) const override
        {
            for (const auto& B : I.mBindings)
                if (B.mBufferRange.Resolve(dynamic_cast<IArdaRHIBuffer*>(B.mResource.Get())->GetDesc()).mByteSize != uint64_t(I.mExtent[0]) * 4)
                    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Add expects one uint32 per element in each view.");
            return {};
        }
    };

    class FSelectionOperand final : public FArdaComputeOperand
    {
    public:
        bool mbInvalidSelection = false;
        FSelectionOperand() : FArdaComputeOperand("sample.selection", {})
        {
            const auto Graphics = [](IArdaRHICommandList&, const FArdaComputeInvocation&) { return FArdaRHIStatus{}; };
            EXPECT_TRUE(RegisterGraphics("first", Graphics));
            EXPECT_FALSE(RegisterGraphics("first", Graphics));
            EXPECT_FALSE(RegisterGraphics("empty", {}));
            EXPECT_TRUE(RegisterGraphics("tuned", Graphics));
            EXPECT_TRUE(RegisterCuda("unsupported_architecture", UINT32_MAX, UINT32_MAX,
                [](const FArdaComputeInvocation&) -> TArdaRHIResult<eastl::vector<FArdaCudaKernel>>
                {
                    ADD_FAILURE() << "Architecture-ineligible builder must never run";
                    return {};
                }));
        }
        size_t SelectVariant(const FArdaComputeInvocation&, const FArdaCudaCapabilities&,
            const eastl::vector<size_t>& Eligible) const override
        { return mbInvalidSelection ? SIZE_MAX : Eligible.back(); }
    };

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
        FArdaComputeInvocation I;
        I.mExtent[0] = Count;
        I.mBindings = {{Src.mValue}, {Dst.mValue}};
        I.mBindings[1].mBufferRange = {16, Count * 4};
        auto Dispatched = FAddOperand().Dispatch(*Cmd.mValue, I, EArdaComputePolicy::RequireCuda);
        ASSERT_TRUE(Dispatched) << Dispatched.mStatus.mMessage.c_str();
        eastl::vector<uint8_t> Readback;
        ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Dst.mValue, Readback));
        ASSERT_TRUE(Cmd.mValue->Close());
        auto Submitted = mDevice->ExecuteCommandList(Cmd.mValue); ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
        ASSERT_TRUE(mDevice->WaitForIdle());
        ASSERT_EQ(Readback.size(), Output.size()*4);
        std::memcpy(Output.data(), Readback.data(), Readback.size());
        for (uint32_t J = 0; J < Count; ++J) ASSERT_EQ(Output[J+4], Input[J]+18) << "element " << J;
        for (uint32_t J : {0u,1u,2u,3u,Count+4,Count+5,Count+6,Count+7}) EXPECT_EQ(Output[J], 0xdeadbeefu);
        if (C.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG) EXPECT_FALSE(mDevice->ExecuteCommandList(Cmd.mValue));
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
                for (auto Byte : CudaBytes) EXPECT_EQ(Byte, 90u);
                for (uint32_t Layer = 0; Layer < D.mArraySize; ++Layer)
                {
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
            (std::string("ArdaCudaFallback") + GetShaderArtifactExtension(GetParam()));
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
        FAddOperand Operand([&](IArdaRHICommandList& Commands, const FArdaComputeInvocation& I)
        {
            FArdaRHIBindingSetDesc BD; BD.mLayout = Layout.mValue;
            for (uint32_t J = 0; J < 2; ++J)
            {
                auto* Buffer = dynamic_cast<IArdaRHIBuffer*>(I.mBindings[J].mResource.Get());
                if (auto S = Commands.SetBufferState(*Buffer, J ? EArdaRHIResourceState::UnorderedAccess :
                    EArdaRHIResourceState::ShaderResource); !S) return S;
                FArdaRHIBindingItem Item;
                Item.mType = J ? EArdaRHIBindingType::StructuredBufferUAV : EArdaRHIBindingType::StructuredBufferSRV;
                Item.mResource = I.mBindings[J].mResource;
                Item.mView.mBufferRange = I.mBindings[J].mBufferRange;
                BD.mItems.push_back(Item);
            }
            auto Set = mDevice->CreateBindingSet(BD); if (!Set) return Set.mStatus;
            FArdaRHIComputeState State; State.mPipeline = Pipeline.mValue; State.mBindings = {Set.mValue};
            if (auto S = Commands.SetComputeState(State); !S) return S;
            Commands.Dispatch((I.mExtent[0] + 127) / 128, 1, 1);
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
            FArdaComputeInvocation I; I.mExtent[0] = Count; I.mBindings = {{A.mValue}, {B.mValue}};
            auto First = Operand.Dispatch(*Cmd.mValue, I, EArdaComputePolicy::RequireGraphics);
            ASSERT_TRUE(First) << First.mStatus.mMessage.c_str(); EXPECT_EQ(First.mValue, "graphics.add");
            I.mBindings = {{B.mValue}, {A.mValue}};
            auto Middle = Operand.Dispatch(*Cmd.mValue, I);
            ASSERT_TRUE(Middle) << Middle.mStatus.mMessage.c_str();
            EXPECT_EQ(Middle.mValue, Share ? "cuda.sequence" : "graphics.add");
            I.mBindings = {{A.mValue}, {B.mValue}};
            ASSERT_TRUE(Operand.Dispatch(*Cmd.mValue, I, EArdaComputePolicy::RequireGraphics));
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
        FSelectionOperand Selection;
        auto Selected = Selection.Dispatch(*Cmd.mValue, {});
        ASSERT_TRUE(Selected); EXPECT_EQ(Selected.mValue, "tuned");
        Selected = Selection.Dispatch(*Cmd.mValue, {}, EArdaComputePolicy::Auto, "first");
        ASSERT_TRUE(Selected); EXPECT_EQ(Selected.mValue, "first");
        EXPECT_FALSE(Selection.Dispatch(*Cmd.mValue, {}, EArdaComputePolicy::RequireCuda));
        Selection.mbInvalidSelection = true;
        EXPECT_FALSE(Selection.Dispatch(*Cmd.mValue, {}));
        FArdaComputeInvocation I; I.mExtent[0] = 64; I.mBindings = {{B.mValue}, {B.mValue}};
        FAddOperand Operand;
        I.mExtent[0] = 65; EXPECT_FALSE(Operand.Dispatch(*Cmd.mValue, I)); I.mExtent[0] = 64;
        EXPECT_FALSE(Operand.Dispatch(*Cmd.mValue, I, EArdaComputePolicy::Auto, "missing"));
        EXPECT_FALSE(Operand.Dispatch(*Cmd.mValue, I, EArdaComputePolicy::RequireGraphics));
        I.mBindings[0].mBufferRange = {4, 256};
        EXPECT_FALSE(Operand.Dispatch(*Cmd.mValue, I));
        I.mBindings[0].mBufferRange = {2, 252};
        EXPECT_FALSE(Operand.Dispatch(*Cmd.mValue, I));
        I.mBindings.pop_back(); EXPECT_FALSE(Operand.Dispatch(*Cmd.mValue, I));
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
        FArdaComputeInvocation I; I.mExtent[0] = Count; I.mBindings = {{Buffer.mValue}, {Buffer.mValue}};
        ASSERT_TRUE(FAddOperand().Dispatch(*Cmd.mValue, I, EArdaComputePolicy::RequireCuda));
        auto Promise = std::make_shared<std::promise<FArdaRHIBufferReadbackResult>>();
        auto Future = Promise->get_future();
        ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHostAsync(*Buffer.mValue,
            [Promise](FArdaRHIBufferReadbackResult Result) { Promise->set_value(eastl::move(Result)); }));
        ASSERT_TRUE(Cmd.mValue->Close()); ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
        I.mBindings.clear(); Buffer.mValue.Reset(); Cmd.mValue.Reset();
        ASSERT_TRUE(mDevice->WaitForIdle());
        ASSERT_EQ(Future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
        auto Result = Future.get(); ASSERT_TRUE(Result); ASSERT_EQ(Result.mValue.size(), Count * 4);
        for (uint32_t J = 0; J < Count; ++J)
        {
            uint32_t Value; std::memcpy(&Value, Result.mValue.data() + J*4, 4);
            ASSERT_EQ(Value, Values[J]+18) << J;
        }
    }

    INSTANTIATE_TEST_SUITE_P(Native, ArdaCudaGpu, testing::Values("native-d3d12", "native-vulkan"));
}
