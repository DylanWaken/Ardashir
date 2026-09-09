#include "Compute/ArdaCudaCompiler.h"

#if defined(ARDA_ENABLE_NVRTC)
#include <nvrtc.h>
#include <climits>
#include <cstdio>
#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace arda
{
#if defined(ARDA_ENABLE_NVRTC)
    namespace
    {
        class FArdaNvrtcLibrary final
        {
        public:
            ~FArdaNvrtcLibrary()
            {
                if (mLibrary)
                {
#if defined(_WIN32)
                    FreeLibrary(mLibrary);
#else
                    dlclose(mLibrary);
#endif
                }
            }

            bool Load(const char* Path)
            {
#if defined(_WIN32)
                mLibrary = LoadLibraryExA(Path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
                mLibrary = dlopen(Path, RTLD_NOW | RTLD_LOCAL);
#endif
                return mLibrary &&
                    LoadFunction(mCreateProgram, "nvrtcCreateProgram") &&
                    LoadFunction(mDestroyProgram, "nvrtcDestroyProgram") &&
                    LoadFunction(mCompileProgram, "nvrtcCompileProgram") &&
                    LoadFunction(mGetPtxSize, "nvrtcGetPTXSize") &&
                    LoadFunction(mGetPtx, "nvrtcGetPTX") &&
                    LoadFunction(mGetLogSize, "nvrtcGetProgramLogSize") &&
                    LoadFunction(mGetLog, "nvrtcGetProgramLog") &&
                    LoadFunction(mGetErrorString, "nvrtcGetErrorString");
            }

            TArdaRHIResult<eastl::string> Compile(const FArdaCudaModuleSource& Source, uint32_t Architecture) const
            {
                if (Source.mLanguage != EArdaCudaSourceLanguage::CudaCpp || !Architecture ||
                    Source.mHeaders.size() > INT_MAX || Source.mCompileOptions.size() >= INT_MAX)
                {
                    return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                        "NVRTC requires CUDA C++ source, a target architecture and representable option/header counts.")};
                }
                eastl::vector<const char*> Headers;
                eastl::vector<const char*> Names;
                for (const auto& Header : Source.mHeaders)
                {
                    Headers.push_back(Header.mCode.c_str());
                    Names.push_back(Header.mName.c_str());
                }
                struct FArdaProgram
                {
                    nvrtcProgram mProgram = nullptr;
                    decltype(&nvrtcDestroyProgram) mDestroy = nullptr;
                    ~FArdaProgram()
                    {
                        if (mProgram)
                        {
                            mDestroy(&mProgram);
                        }
                    }
                } Program{nullptr, mDestroyProgram};
                auto Result = mCreateProgram(&Program.mProgram, Source.mCode.c_str(), Source.mName.c_str(),
                    static_cast<int>(Headers.size()), Headers.data(), Names.data());
                if (Result != NVRTC_SUCCESS)
                {
                    return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, mGetErrorString(Result))};
                }
                char Target[64];
                std::snprintf(Target, sizeof(Target), "--gpu-architecture=compute_%u", Architecture);
                eastl::vector<const char*> Options;
                for (const auto& Option : Source.mCompileOptions)
                {
                    // A module's cache key and selected architecture must describe the
                    // code actually generated. Target selection belongs to this adapter.
                    if (Option.find("--gpu-architecture") == 0 || Option.find("-arch") == 0)
                    {
                        return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                            "NVRTC module options must not override the requested target architecture.")};
                    }
                    Options.push_back(Option.c_str());
                }
                Options.push_back(Target);
                Result = mCompileProgram(Program.mProgram, static_cast<int>(Options.size()), Options.data());
                if (Result != NVRTC_SUCCESS)
                {
                    eastl::string Message = mGetErrorString(Result);
                    size_t LogSize = 0;
                    if (mGetLogSize(Program.mProgram, &LogSize) == NVRTC_SUCCESS && LogSize > 1)
                    {
                        eastl::vector<char> Log(LogSize);
                        if (mGetLog(Program.mProgram, Log.data()) == NVRTC_SUCCESS)
                        {
                            Message += "\n";
                            Message += Log.data();
                        }
                    }
                    return {{}, {EArdaRHIResult::BackendFailure, eastl::move(Message)}};
                }
                size_t PtxSize = 0;
                Result = mGetPtxSize(Program.mProgram, &PtxSize);
                if (Result != NVRTC_SUCCESS || PtxSize <= 1)
                {
                    return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
                        Result != NVRTC_SUCCESS ? mGetErrorString(Result) : "NVRTC produced no PTX.")};
                }
                eastl::vector<char> Ptx(PtxSize);
                Result = mGetPtx(Program.mProgram, Ptx.data());
                if (Result != NVRTC_SUCCESS)
                {
                    return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, mGetErrorString(Result))};
                }
                return {eastl::string(Ptx.data(), PtxSize - 1), {}};
            }

        private:
            template<typename T> bool LoadFunction(T& OutFunction, const char* Name)
            {
#if defined(_WIN32)
                OutFunction = reinterpret_cast<T>(GetProcAddress(mLibrary, Name));
#else
                OutFunction = reinterpret_cast<T>(dlsym(mLibrary, Name));
#endif
                return OutFunction != nullptr;
            }
#if defined(_WIN32)
            HMODULE mLibrary = nullptr;
#else
            void* mLibrary = nullptr;
#endif
            decltype(&nvrtcCreateProgram) mCreateProgram = nullptr;
            decltype(&nvrtcDestroyProgram) mDestroyProgram = nullptr;
            decltype(&nvrtcCompileProgram) mCompileProgram = nullptr;
            decltype(&nvrtcGetPTXSize) mGetPtxSize = nullptr;
            decltype(&nvrtcGetPTX) mGetPtx = nullptr;
            decltype(&nvrtcGetProgramLogSize) mGetLogSize = nullptr;
            decltype(&nvrtcGetProgramLog) mGetLog = nullptr;
            decltype(&nvrtcGetErrorString) mGetErrorString = nullptr;
        };
    }
#endif

    TArdaRHIResult<FArdaCudaModuleCompiler> CreateArdaNvrtcCompiler(const char* LibraryPath)
    {
        if (!LibraryPath || !*LibraryPath)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "NVRTC library path is required.")};
        }
#if defined(ARDA_ENABLE_NVRTC)
        auto Library = eastl::make_shared<FArdaNvrtcLibrary>();
        if (!Library->Load(LibraryPath))
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "NVRTC library or required exports are unavailable.")};
        }
        return {FArdaCudaModuleCompiler([Library](const FArdaCudaModuleSource& Source, uint32_t Architecture)
        {
            return Library->Compile(Source, Architecture);
        }), {}};
#else
        return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
            "NVRTC adapter was built without ARDASHIR_NVRTC_INCLUDE_DIR.")};
#endif
    }
}
