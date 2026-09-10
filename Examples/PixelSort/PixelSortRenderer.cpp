#include "PixelSortRenderer.h"
#include "ShaderStructs/ArdaShaderCompiler.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace arda
{
    namespace
    {
        void Check(FArdaRHIStatus Status)
        { if (!Status) throw std::runtime_error(Status.mMessage.c_str()); }
        template<class T> T Take(TArdaRHIResult<T> Result)
        { Check(Result.mStatus); return eastl::move(Result.mValue); }
        struct FFrameConstants { uint32_t mWidth, mHeight; float mTime; uint32_t mOriginal; };
        uint32_t Luma(uint32_t Pixel)
        { return (54 * (Pixel & 255) + 183 * ((Pixel >> 8) & 255) + 19 * ((Pixel >> 16) & 255)) >> 8; }

        void VerifySort(const std::vector<uint32_t>& Input, const std::vector<uint32_t>& Output,
            uint32_t Width, uint32_t Height, uint32_t Channel, uint32_t Threshold)
        {
            const bool Vertical = Height > Width;
            const uint32_t Tile = Vertical ? 1024 : 512, Length = Vertical ? Height : Width, Lines = Vertical ? Width : Height;
            auto Expected = Input;
            for (uint32_t Line = 0; Line < Lines; ++Line)
            for (uint32_t Begin = 0; Begin < Length; Begin += Tile)
            {
                const uint32_t Count = std::min(Tile, Length - Begin);
                const auto Index = [&](uint32_t I) { return Vertical ? (Begin + I) * Width + Line : Line * Width + Begin + I; };
                std::vector<uint32_t> Values(Count);
                for (uint32_t I = 0; I < Count; ++I) Values[I] = Input[Index(I)];
                for (uint32_t I = 0; I < Count;)
                {
                    if (Luma(Values[I]) < Threshold) { ++I; continue; }
                    const auto First = I;
                    while (I < Count && Luma(Values[I]) >= Threshold) ++I;
                    std::stable_sort(Values.begin() + First, Values.begin() + I, [Channel](uint32_t A, uint32_t B) {
                        return ((A >> (Channel * 8)) & 255) < ((B >> (Channel * 8)) & 255);
                    });
                }
                for (uint32_t I = 0; I < Count; ++I) Expected[Index(I)] = Values[I];
            }
            const auto Mismatch = std::mismatch(Expected.begin(), Expected.end(), Output.begin());
            if (Mismatch.first != Expected.end())
                throw std::runtime_error("CUDA radix output differs from the CPU stable-sort reference at pixel " +
                    std::to_string(Mismatch.first - Expected.begin()));
            if (std::all_of(Input.begin(), Input.end(), [&](uint32_t V) { return V == Input.front(); }))
                throw std::runtime_error("Noise compute shader produced a uniform image.");
            std::printf("Verified %ux%u, %s, channel %u, %u threads, tile %u: exact RGBA match.\n",
                Width, Height, Vertical ? "columns" : "rows", Channel, Vertical ? 256 : 128, Tile);
        }

        void SavePng(const std::filesystem::path& Path, uint32_t Width, uint32_t Height, std::vector<uint32_t> Pixels, EArdaRHIFormat Format)
        {
            using Microsoft::WRL::ComPtr;
            const auto Checked = [](HRESULT Result) { if (FAILED(Result)) throw std::runtime_error("PNG encoding failed."); };
            if (Format == EArdaRHIFormat::RGBA8UNorm || Format == EArdaRHIFormat::SRGBA8UNorm)
                for (auto& P : Pixels) P = (P & 0xff00ff00u) | ((P & 255) << 16) | ((P >> 16) & 255);
            else if (Format != EArdaRHIFormat::BGRA8UNorm && Format != EArdaRHIFormat::SBGRA8UNorm)
                throw std::runtime_error("Capture requires an RGBA8/BGRA8 swap chain.");
            ComPtr<IWICImagingFactory> Factory;
            Checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory)));
            ComPtr<IWICStream> Stream; Checked(Factory->CreateStream(&Stream));
            Checked(Stream->InitializeFromFilename(Path.c_str(), GENERIC_WRITE));
            ComPtr<IWICBitmapEncoder> Encoder; Checked(Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &Encoder));
            Checked(Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache));
            ComPtr<IWICBitmapFrameEncode> Frame; Checked(Encoder->CreateNewFrame(&Frame, nullptr));
            Checked(Frame->Initialize(nullptr)); Checked(Frame->SetSize(Width, Height));
            WICPixelFormatGUID PixelFormat = GUID_WICPixelFormat32bppBGRA;
            Checked(Frame->SetPixelFormat(&PixelFormat));
            if (PixelFormat != GUID_WICPixelFormat32bppBGRA) throw std::runtime_error("PNG encoder changed its pixel format.");
            Checked(Frame->WritePixels(Height, Width * 4, uint32_t(Pixels.size() * 4), reinterpret_cast<BYTE*>(Pixels.data())));
            Checked(Frame->Commit()); Checked(Encoder->Commit());
        }
    }

    FPixelSortRenderer::~FPixelSortRenderer() { if (mDevice) (void)mDevice->WaitForIdle(); }
    void FPixelSortRenderer::Initialize(EArdaRHIFormat Format, const std::filesystem::path& Directory)
    {
        const auto Source = (Directory / "Shaders/PixelSort.hlsl").string();
        FArdaShaderTypeRegistration Noise("PixelSortNoise", Source.c_str(), "PixelSortNoise", "NoiseCS", EArdaRHIShaderStage::Compute, nullptr);
        FArdaShaderTypeRegistration VS("PixelSortVertex", Source.c_str(), "PixelSortVertex", "PresentVS", EArdaRHIShaderStage::Vertex, nullptr);
        FArdaShaderTypeRegistration PS("PixelSortPixel", Source.c_str(), "PixelSortPixel", "PresentPS", EArdaRHIShaderStage::Pixel, nullptr);
        auto Compiler = GetShaderCompilerConfiguration();
        Compiler.mCompilerExecutable = (Directory / "ShaderCompiler/dxc.exe").string().c_str();
        Compiler.mbCompileMissingArtifacts = Compiler.mbCompileOutdatedArtifacts = true;
        ConfigureShaderCompiler(Compiler);
        const auto Cache = Directory / ".arda-cache/PixelSort";
        const auto Compiled = EnsureRegisteredShaderArtifacts(Cache, GetBackendConfiguration().mBackendName.c_str());
        if (!Compiled) throw std::runtime_error(Compiled.mDiagnostics.empty() ? "Shader compilation failed." : Compiled.mDiagnostics.front().mMessage.c_str());
        const auto Shader = [&](const char* Name, const char* Entry, EArdaRHIShaderStage Stage) {
            const auto Code = LoadShaderBytecode(Cache / (std::string(Name) + GetShaderArtifactExtension(GetBackendConfiguration().mBackendName.c_str())));
            if (!Code) throw std::runtime_error(Code.mDiagnostic.mMessage.c_str());
            FArdaRHIShaderDesc D; D.mStage = Stage; D.mBytecode = Code.mBytecode.data(); D.mBytecodeSize = Code.mBytecode.size(); D.mEntryPoint = Entry;
            return Take(mDevice->CreateShader(D));
        };
        FArdaRHIBindingLayoutDesc Layout;
        Layout.mVisibility = EArdaRHIShaderStage::Compute;
        Layout.mItems = {{0, 1, EArdaRHIBindingType::ConstantBuffer}, {0, 1, EArdaRHIBindingType::TextureUAV}};
        mNoiseLayout = Take(mDevice->CreateBindingLayout(Layout));
        FArdaRHIComputePipelineDesc Compute;
        Compute.mComputeShader = Shader("PixelSortNoise", "NoiseCS", EArdaRHIShaderStage::Compute);
        Compute.mBindingLayouts.push_back(mNoiseLayout);
        mNoisePipeline = Take(mDevice->CreateComputePipeline(Compute));
        Layout.mVisibility = EArdaRHIShaderStage::Pixel;
        Layout.mItems = {{0, 1, EArdaRHIBindingType::ConstantBuffer}, {0, 1, EArdaRHIBindingType::TextureSRV}, {1, 1, EArdaRHIBindingType::TextureSRV}};
        mPresentLayout = Take(mDevice->CreateBindingLayout(Layout));
        FArdaRHIGraphicsPipelineDesc Graphics;
        Graphics.mVertexShader = Shader("PixelSortVertex", "PresentVS", EArdaRHIShaderStage::Vertex);
        Graphics.mPixelShader = Shader("PixelSortPixel", "PresentPS", EArdaRHIShaderStage::Pixel);
        Graphics.mBindingLayouts.push_back(mPresentLayout); Graphics.mColorFormats.push_back(Format);
        Graphics.mDepthStencilState.mbDepthTest = Graphics.mDepthStencilState.mbDepthWrite = false;
        Graphics.mRasterState.mCullMode = EArdaRHICullMode::None;
        mPresentPipeline = Take(mDevice->CreateGraphicsPipeline(Graphics));
        FArdaRHIBufferDesc Constants; Constants.mByteSize = 256; Constants.mUsage = EArdaRHIBufferUsage::Constant;
        mConstants = Take(mDevice->CreateBuffer(Constants));
        Check(mOperand.GetOperandSupport());
    }
    void FPixelSortRenderer::Resize(uint32_t Width, uint32_t Height)
    {
        if (Width == mWidth && Height == mHeight) return;
        Check(mDevice->WaitForIdle());
        FArdaRHITextureDesc D; D.mWidth = Width; D.mHeight = Height;
        D.mFormat = EArdaRHIFormat::RGBA8UInt; D.mbCudaInterop = true;
        D.mUsage = EArdaRHITextureUsage::UnorderedAccess | EArdaRHITextureUsage::ShaderResource;
        D.mDebugName = "PixelSort animated noise"; mNoise = Take(mDevice->CreateTexture(D));
        D.mDebugName = "PixelSort CUDA output"; mSorted = Take(mDevice->CreateTexture(D));
        FArdaRHIBindingSetDesc B; B.mLayout = mNoiseLayout;
        B.mItems = {{0, 0, EArdaRHIBindingType::ConstantBuffer, mConstants, {}}, {0, 0, EArdaRHIBindingType::TextureUAV, mNoise, {}}};
        mNoiseBindings = Take(mDevice->CreateBindingSet(B));
        B.mLayout = mPresentLayout;
        B.mItems = {{0, 0, EArdaRHIBindingType::ConstantBuffer, mConstants, {}}, {0, 0, EArdaRHIBindingType::TextureSRV, mSorted, {}},
            {1, 0, EArdaRHIBindingType::TextureSRV, mNoise, {}}};
        mPresentBindings = Take(mDevice->CreateBindingSet(B));
        mWidth = Width; mHeight = Height;
    }
    std::vector<uint32_t> FPixelSortRenderer::ReadPixels(const FArdaRHIStagingTextureRef& Staging, uint32_t Width, uint32_t Height)
    {
        auto Mapped = Take(mDevice->MapStagingTexture(Staging, {}, EArdaRHICpuAccess::Read));
        std::vector<uint32_t> Pixels(size_t(Width) * Height);
        for (uint32_t Y = 0; Y < Height; ++Y)
            std::memcpy(Pixels.data() + size_t(Y) * Width, static_cast<const uint8_t*>(Mapped.mData) + Y * Mapped.mRowPitch, Width * 4);
        Check(mDevice->UnmapStagingTexture(Staging));
        return Pixels;
    }
    void FPixelSortRenderer::Render(IArdaSwapChain& SwapChain, float Time, uint32_t Channel, uint32_t Threshold,
        bool Original, bool Verify, const std::filesystem::path& Capture)
    {
        Resize(SwapChain.GetWidth(), SwapChain.GetHeight());
        FArdaRHIFramebufferRef Framebuffer;
        if (!SwapChain.AcquireFrame(Framebuffer)) throw std::runtime_error(SwapChain.GetError().c_str());
        auto Commands = Take(mDevice->CreateCommandList(EArdaRHIQueueType::Graphics));
        Check(Commands->Open());
        const FFrameConstants Frame{mWidth, mHeight, Time, Original ? 1u : 0u};
        Check(Commands->WriteBuffer(*mConstants, &Frame, sizeof(Frame)));
        Check(Commands->SetBufferState(*mConstants, EArdaRHIResourceState::ConstantBuffer));
        Check(Commands->SetTextureState(*mNoise, {}, EArdaRHIResourceState::UnorderedAccess));
        FArdaRHIComputeState Noise; Noise.mPipeline = mNoisePipeline; Noise.mBindings.push_back(mNoiseBindings);
        Check(Commands->SetComputeState(Noise)); Commands->Dispatch((mWidth + 7) / 8, (mHeight + 7) / 8);
        FPixelSortParameters Parameters;
        Parameters.mInput.mTexture = mNoise; Parameters.mOutput.mTexture = mSorted;
        Parameters.mWidth = mWidth; Parameters.mHeight = mHeight; Parameters.mChannel = Channel; Parameters.mThreshold = Threshold;
        Check(mOperand.DispatchDeferred(*Commands, Parameters));
        // CUDA releases the shared textures in UAV state; sampling needs SRV state.
        Check(Commands->SetTextureState(*mSorted, {}, EArdaRHIResourceState::PixelShaderResource));
        Check(Commands->SetTextureState(*mNoise, {}, EArdaRHIResourceState::PixelShaderResource));
        FArdaRHIGraphicsState Present;
        Present.mPipeline = mPresentPipeline; Present.mFramebuffer = Framebuffer; Present.mBindings.push_back(mPresentBindings);
        Present.mViewports.push_back({0.f, float(mWidth), 0.f, float(mHeight), 0.f, 1.f});
        Present.mScissors.push_back({0, int32_t(mWidth), 0, int32_t(mHeight)});
        Check(Commands->SetGraphicsState(Present)); Commands->Draw({3});
        FArdaRHIStagingTextureRef InputReadback, OutputReadback, FrameReadback;
        const auto Readback = [&](const FArdaRHITextureRef& Texture) {
            FArdaRHIStagingTextureDesc D; D.mTexture = Texture->GetDesc(); D.mTexture.mbCudaInterop = false; D.mCpuAccess = EArdaRHICpuAccess::Read;
            auto Result = Take(mDevice->CreateStagingTexture(D));
            Check(Commands->CopyTextureToStaging(*Result, {}, *Texture, {})); return Result;
        };
        if (Verify) { InputReadback = Readback(mNoise); OutputReadback = Readback(mSorted); }
        const auto BackBuffer = Framebuffer->GetDesc().mColorAttachments.front().mTexture;
        if (!Capture.empty()) FrameReadback = Readback(BackBuffer);
        Check(Commands->SetTextureState(*BackBuffer, {}, EArdaRHIResourceState::Present));
        Check(Commands->Close()); SwapChain.PrepareSubmit(); Take(mDevice->ExecuteCommandList(Commands));
        if (!SwapChain.Present()) throw std::runtime_error(SwapChain.GetError().c_str());
        if (Verify || FrameReadback)
        {
            Check(mDevice->WaitForIdle());
            if (Verify) VerifySort(ReadPixels(InputReadback, mWidth, mHeight), ReadPixels(OutputReadback, mWidth, mHeight), mWidth, mHeight, Channel, Threshold);
            if (FrameReadback) SavePng(Capture, mWidth, mHeight, ReadPixels(FrameReadback, mWidth, mHeight), BackBuffer->GetDesc().mFormat);
        }
        mDevice->RunGarbageCollection();
    }
}
