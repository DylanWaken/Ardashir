#include "PixelSortWindow.h"
#include "PixelSortRenderer.h"
#include <atomic>
#include <objbase.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace arda
{
    namespace
    {
        struct FOptions
        {
            eastl::string mBackend = "native-d3d12";
            EArdaCudaExecutionMode mMode = EArdaCudaExecutionMode::Automatic;
            uint32_t mFrames = 0, mWidth = 960, mHeight = 1200, mChannel = 0, mThreshold = 48;
            float mTime = 0;
            bool mbHidden = false, mbVerify = false, mbResizeTest = false, mbFixedTime = false, mbValidation = false;
            std::filesystem::path mCapture;
        };
        struct FDiagnostics final : IArdaDiagnosticCallback
        {
            std::atomic<uint32_t> mErrors{0};
            void Message(EArdaDiagnosticSeverity Severity, const char* Text) override
            {
                if (Severity == EArdaDiagnosticSeverity::Error || Severity == EArdaDiagnosticSeverity::Fatal) ++mErrors;
                if (Severity >= EArdaDiagnosticSeverity::Warning) std::fprintf(stderr, "%s\n", Text ? Text : "");
            }
        };
        struct FBackendLifetime
        {
            eastl::unique_ptr<IArdaSwapChain> mSwapChain;
            ~FBackendLifetime()
            {
                if (mSwapChain) { mSwapChain->WaitForIdle(); mSwapChain.reset(); }
                if (IsBackendInitialized()) ShutdownBackend();
            }
        };
        uint32_t Number(const std::string& Text)
        {
            size_t End = 0; const auto Value = std::stoul(Text, &End);
            if (End != Text.size() || Text.empty() || Text[0] == '-' || Value > UINT32_MAX) throw std::runtime_error("Expected a nonnegative integer.");
            return uint32_t(Value);
        }
        int Run(int Count, char** Args)
        {
            FOptions O;
            for (int I = 1; I < Count; ++I)
            {
                const std::string A = Args[I];
                const auto Next = [&]() -> std::string { if (++I >= Count) throw std::runtime_error("Missing argument for " + A); return Args[I]; };
                if (A == "--hidden") O.mbHidden = true;
                else if (A == "--verify") O.mbVerify = true;
                else if (A == "--validation") O.mbValidation = true;
                else if (A == "--resize-test") O.mbResizeTest = true;
                else if (A == "--frames") O.mFrames = Number(Next());
                else if (A == "--width") O.mWidth = Number(Next());
                else if (A == "--height") O.mHeight = Number(Next());
                else if (A == "--channel") O.mChannel = Number(Next());
                else if (A == "--threshold") O.mThreshold = Number(Next());
                else if (A == "--time")
                {
                    const auto Text = Next(); size_t End = 0; O.mTime = std::stof(Text, &End);
                    if (End != Text.size()) throw std::runtime_error("Expected a finite time in seconds.");
                    O.mbFixedTime = true;
                }
                else if (A == "--capture") O.mCapture = Next();
                else if (A == "--backend")
                {
                    const auto V = Next(); if (V != "d3d12" && V != "vulkan") throw std::runtime_error("Backend must be d3d12 or vulkan.");
                    O.mBackend = V == "d3d12" ? "native-d3d12" : "native-vulkan";
                }
                else if (A == "--cuda-mode")
                {
                    const auto V = Next();
                    if (V == "auto") O.mMode = EArdaCudaExecutionMode::Automatic;
                    else if (V == "context") O.mMode = EArdaCudaExecutionMode::ContextSwitch;
                    else if (V == "graphics") O.mMode = EArdaCudaExecutionMode::GraphicsQueue;
                    else throw std::runtime_error("CUDA mode must be auto, context or graphics.");
                }
                else if (A == "--help")
                {
                    std::puts("PixelSort [--backend d3d12|vulkan] [--cuda-mode auto|context|graphics] [--width N --height N]\n"
                        "  [--frames N --hidden --verify --resize-test --validation] [--channel 0|1|2 --threshold 0..255] [--time T --capture image.png]\n"
                        "C: RGB sort key, Space: pause, Tab: original, Up/Down: dark-run threshold, Esc: close. Resize to change direction.");
                    return 0;
                }
                else throw std::runtime_error("Unknown option: " + A);
            }
            if (!O.mWidth || !O.mHeight || O.mWidth > 8192 || O.mHeight > 8192 || O.mChannel > 2 || O.mThreshold > 255 || !std::isfinite(O.mTime))
                throw std::runtime_error("Use dimensions 1..8192, channel 0..2 and threshold 0..255.");
            if (O.mbResizeTest && !O.mFrames) O.mFrames = 6;
            FPixelSortWindow Window; Window.Create(O.mWidth, O.mHeight, O.mbHidden);
            Window.mChannel = O.mChannel; Window.mThreshold = O.mThreshold;
            FDiagnostics Diagnostics;
            FArdaBackendConfiguration Config; Config.mBackendName = O.mBackend; Config.mCudaExecutionMode = O.mMode;
            Config.mbEnableValidation = O.mbValidation || O.mbVerify; Config.mMessageCallback = &Diagnostics;
            Config.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
            if (!ConfigureBackend(Config)) throw std::runtime_error(GetBackendError().c_str());
            FBackendLifetime Backend;
            const auto Init = InitializeBackendForPresentation(Window, Window.mWidth, Window.mHeight, Backend.mSwapChain);
            if (Init != EArdaInitializeResult::Success) { std::fprintf(stderr, "%s\n", GetBackendError().c_str()); return Init == EArdaInitializeResult::Unavailable || Init == EArdaInitializeResult::ValidationUnavailable ? 77 : 1; }
            const auto Caps = GetDevice()->GetCudaCapabilities();
            if (!Caps || !Caps.mbSurfaceAccess)
            { std::fprintf(stderr, "%s\n", (Caps ? Caps.mSurfaceUnavailableReason : Caps.mUnavailableReason).c_str()); return 77; }
            wchar_t Executable[32768]; const auto PathLength = GetModuleFileNameW(nullptr, Executable, 32768);
            if (!PathLength || PathLength == 32768) throw std::runtime_error("Cannot locate executable.");
            FPixelSortRenderer Renderer(GetDevice());
            Renderer.Initialize(Backend.mSwapChain->GetFormat(), std::filesystem::path(Executable).parent_path());
            const char* Mode = Caps.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG ? "D3D12 CiG" :
                Caps.mLaunchMode == EArdaCudaLaunchMode::VulkanCiG ? "Vulkan CiG" : "CUDA context";
            std::printf("PixelSort: %s, %s, SM %u\n", O.mBackend.c_str(), Mode, Caps.mComputeCapability);
            auto Previous = std::chrono::steady_clock::now(); float Time = O.mTime;
            uint32_t Frame = 0;
            while (Window.Pump())
            {
                if (O.mbResizeTest)
                {
                    constexpr uint32_t Sizes[][2] = {{321, 197}, {197, 321}, {257, 193}, {193, 257}, {1537, 197}, {197, 1537}};
                    Window.Resize(Sizes[Frame % 6][0], Sizes[Frame % 6][1]);
                    Window.Pump(); Window.mChannel = Frame % 3; Window.mThreshold = (Frame % 3) * 48;
                }
                const bool Resized = Window.ConsumeResize();
                if (!Window.mWidth || !Window.mHeight) { WaitMessage(); Previous = std::chrono::steady_clock::now(); continue; }
                if (Resized && !Backend.mSwapChain->Resize(Window.mWidth, Window.mHeight)) throw std::runtime_error(Backend.mSwapChain->GetError().c_str());
                const auto Now = std::chrono::steady_clock::now();
                if (!Window.mbPaused && !O.mbFixedTime) Time += std::chrono::duration<float>(Now - Previous).count();
                Previous = Now;
                char Title[256]; const bool Vertical = Window.mHeight > Window.mWidth;
                std::snprintf(Title, sizeof(Title), "Pixel Sort | %s | %c channel | %s / %u threads | C: channel  Space: pause  Tab: original  Up/Down: threshold %u",
                    Mode, "RGB"[Window.mChannel], Vertical ? "Vertical" : "Horizontal", Vertical ? 256 : 128, Window.mThreshold);
                Window.SetTitle(Title);
                const bool Last = O.mFrames ? Frame + 1 == O.mFrames : Frame == 0;
                Renderer.Render(*Backend.mSwapChain, Time, Window.mChannel, Window.mThreshold, Window.mbOriginal, O.mbVerify,
                    Last ? O.mCapture : std::filesystem::path{});
                if (Diagnostics.mErrors) throw std::runtime_error("Native graphics validation reported errors.");
                ++Frame;
                if (O.mFrames && Frame >= O.mFrames) break;
            }
            return 0;
        }
    }
}
int main(int Count, char** Args)
{
    const auto Com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int Result = 1;
    try { Result = arda::Run(Count, Args); }
    catch (const std::exception& Error) { std::fprintf(stderr, "PixelSort: %s\n", Error.what()); }
    if (SUCCEEDED(Com)) CoUninitialize();
    return Result;
}
