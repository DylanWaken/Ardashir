/** @file ArdaCudaInterop.cpp
 * Loads the CUDA driver dynamically for D3D12 CiG or deferred ordinary-context PTX launches.
 * Shared D3D12/Vulkan imports, cached modules and streams follow graphics-fence lifetime.
 */
#include "ArdaCudaInterop.h"

#if defined(ARDA_ENABLE_CUDA)
#include <cuda.h>
#if CUDA_VERSION < 12000
#error Ardashir CUDA interop requires CUDA 12.0 or newer headers.
#endif
#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#include <EASTL/algorithm.h>
#include <EASTL/shared_ptr.h>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <string>

namespace arda
{
    namespace
    {
        // Resolve versioned driver entry points without creating a mandatory CUDA DLL import.
        struct FArdaCudaDriver
        {
#if defined(_WIN32)
            HMODULE mLibrary = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            void* GetSymbol(const char* Name) const { return reinterpret_cast<void*>(GetProcAddress(mLibrary, Name)); }
#else
            void* mLibrary = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
            void* GetSymbol(const char* Name) const { return dlsym(mLibrary, Name); }
#endif
#define ARDA_CUDA_FUNCTION(Name, Symbol) decltype(&::Name) Name = mLibrary ? reinterpret_cast<decltype(Name)>(GetSymbol(Symbol)) : nullptr
            ARDA_CUDA_FUNCTION(cuInit, "cuInit");
            ARDA_CUDA_FUNCTION(cuDeviceGetCount, "cuDeviceGetCount");
            ARDA_CUDA_FUNCTION(cuDeviceGet, "cuDeviceGet");
            ARDA_CUDA_FUNCTION(cuDeviceGetLuid, "cuDeviceGetLuid");
            ARDA_CUDA_FUNCTION(cuDeviceGetUuid, "cuDeviceGetUuid_v2");
            ARDA_CUDA_FUNCTION(cuDeviceGetAttribute, "cuDeviceGetAttribute");
            using FArdaCudaCreateContext = CUresult (CUDAAPI*)(CUcontext*, unsigned int, CUdevice);
            FArdaCudaCreateContext mCreateContext = mLibrary ? reinterpret_cast<FArdaCudaCreateContext>(GetSymbol("cuCtxCreate_v2")) : nullptr;
#if CUDA_VERSION >= 13030
            ARDA_CUDA_FUNCTION(cuCtxCreate, "cuCtxCreate_v4");
#endif
            ARDA_CUDA_FUNCTION(cuCtxDestroy, "cuCtxDestroy_v2");
            ARDA_CUDA_FUNCTION(cuCtxPushCurrent, "cuCtxPushCurrent_v2");
            ARDA_CUDA_FUNCTION(cuCtxPopCurrent, "cuCtxPopCurrent_v2");
            ARDA_CUDA_FUNCTION(cuCtxGetLimit, "cuCtxGetLimit");
            ARDA_CUDA_FUNCTION(cuStreamCreate, "cuStreamCreate");
            ARDA_CUDA_FUNCTION(cuStreamDestroy, "cuStreamDestroy_v2");
#if CUDA_VERSION >= 13030
            ARDA_CUDA_FUNCTION(cuStreamBeginCaptureToCig, "cuStreamBeginCaptureToCig");
            ARDA_CUDA_FUNCTION(cuStreamEndCaptureToCig, "cuStreamEndCaptureToCig");
#endif
            ARDA_CUDA_FUNCTION(cuStreamSynchronize, "cuStreamSynchronize");
            ARDA_CUDA_FUNCTION(cuModuleLoadDataEx, "cuModuleLoadDataEx");
            ARDA_CUDA_FUNCTION(cuModuleUnload, "cuModuleUnload");
            ARDA_CUDA_FUNCTION(cuModuleGetFunction, "cuModuleGetFunction");
            // Parameter metadata is optional on older CUDA drivers/SDKs.
            using FArdaCudaGetParamInfo = CUresult (CUDAAPI*)(CUfunction, size_t, size_t*, size_t*);
            FArdaCudaGetParamInfo cuFuncGetParamInfo = mLibrary ? reinterpret_cast<FArdaCudaGetParamInfo>(GetSymbol("cuFuncGetParamInfo")) : nullptr;
            ARDA_CUDA_FUNCTION(cuFuncGetAttribute, "cuFuncGetAttribute");
            ARDA_CUDA_FUNCTION(cuLaunchKernel, "cuLaunchKernel");
            ARDA_CUDA_FUNCTION(cuImportExternalMemory, "cuImportExternalMemory");
            ARDA_CUDA_FUNCTION(cuExternalMemoryGetMappedBuffer, "cuExternalMemoryGetMappedBuffer");
            ARDA_CUDA_FUNCTION(cuExternalMemoryGetMappedMipmappedArray, "cuExternalMemoryGetMappedMipmappedArray");
            ARDA_CUDA_FUNCTION(cuMipmappedArrayGetLevel, "cuMipmappedArrayGetLevel");
            ARDA_CUDA_FUNCTION(cuSurfObjectCreate, "cuSurfObjectCreate");
            ARDA_CUDA_FUNCTION(cuSurfObjectDestroy, "cuSurfObjectDestroy");
            ARDA_CUDA_FUNCTION(cuMemFree, "cuMemFree_v2");
            ARDA_CUDA_FUNCTION(cuMipmappedArrayDestroy, "cuMipmappedArrayDestroy");
            ARDA_CUDA_FUNCTION(cuDestroyExternalMemory, "cuDestroyExternalMemory");
            ARDA_CUDA_FUNCTION(cuGetErrorName, "cuGetErrorName");
#undef ARDA_CUDA_FUNCTION
            ~FArdaCudaDriver()
            {
                if (!mLibrary) return;
#if defined(_WIN32)
                FreeLibrary(mLibrary);
#else
                dlclose(mLibrary);
#endif
            }
            bool IsComplete() const
            {
                return cuInit && cuDeviceGetCount && cuDeviceGet && cuDeviceGetUuid && cuDeviceGetAttribute &&
                    mCreateContext && cuCtxDestroy && cuCtxPushCurrent && cuCtxPopCurrent && cuCtxGetLimit &&
                    cuStreamCreate && cuStreamDestroy && cuStreamSynchronize &&
                    cuModuleLoadDataEx && cuModuleUnload && cuModuleGetFunction && cuFuncGetAttribute &&
                    cuLaunchKernel && cuImportExternalMemory && cuExternalMemoryGetMappedBuffer &&
                    cuExternalMemoryGetMappedMipmappedArray && cuMipmappedArrayGetLevel && cuSurfObjectCreate &&
                    cuSurfObjectDestroy && cuMemFree && cuMipmappedArrayDestroy && cuDestroyExternalMemory && cuGetErrorName;
            }
            FArdaRHIStatus Check(CUresult Result, const char* Operation) const
            {
                if (Result == CUDA_SUCCESS) return {};
                const char* Error = nullptr;
                if (cuGetErrorName) cuGetErrorName(Result, &Error);
                eastl::string Message = Operation;
                Message += ": "; Message += Error ? Error : "unknown CUDA driver error";
                return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Message.c_str());
            }
        };

        // CUDA context stacks are thread-local; restore the caller's context on every exit.
        struct FArdaCudaScope
        {
            FArdaCudaDriver& mDriver;
            CUresult mResult;
            FArdaCudaScope(FArdaCudaDriver& Driver, CUcontext Context) : mDriver(Driver), mResult(Driver.cuCtxPushCurrent(Context)) {}
            ~FArdaCudaScope() { if (mResult == CUDA_SUCCESS) { CUcontext Previous; mDriver.cuCtxPopCurrent(&Previous); } }
        };

        struct FArdaCudaContext;
        // The mapping retains its context; the native resource owns the mapping and outlives it.
        struct FArdaCudaMapping final : IArdaCudaMapping
        {
            eastl::shared_ptr<FArdaCudaContext> mContext;
            CUexternalMemory mMemory = nullptr;
            CUdeviceptr mBuffer = 0;
            CUmipmappedArray mArray = nullptr;
            eastl::vector<CUsurfObject> mSurfaces;
            ~FArdaCudaMapping() override;
            uint64_t GetArgument(uint32_t Mip, uint64_t Offset) const override
            { return mBuffer ? mBuffer + Offset : mSurfaces.at(Mip); }
        };

        struct FArdaCudaModule
        {
            eastl::shared_ptr<FArdaCudaDriver> mDriver;
            CUcontext mContext = nullptr;
            CUmodule mModule = nullptr;
            ~FArdaCudaModule()
            {
                FArdaCudaScope Scope(*mDriver, mContext);
                if (Scope.mResult == CUDA_SUCCESS && mModule) mDriver->cuModuleUnload(mModule);
            }
        };

        // Stream and module references survive recording and are retired with the submitted list.
        struct FArdaCudaBatch final : IArdaCudaBatch
        {
            explicit FArdaCudaBatch(eastl::shared_ptr<FArdaCudaContext> Context) : mContext(eastl::move(Context)) {}
            ~FArdaCudaBatch() override;
            const void* GetIdentity() const noexcept override { return this; }
            FArdaRHIStatus Record(void*, const eastl::vector<FArdaCudaKernel>&, const eastl::vector<uint64_t>&) override;
            FArdaRHIStatus ValidateSubmit() const override;
            void MarkSubmitted() override;
            FArdaRHIStatus Execute() override;
            FArdaRHIStatus Launch(const eastl::vector<FArdaCudaKernel>&, const eastl::vector<CUfunction>&,
                const eastl::vector<uint64_t>&);
            struct FArdaCudaLaunch
            {
                eastl::vector<FArdaCudaKernel> mKernels;
                eastl::vector<CUfunction> mFunctions;
                eastl::vector<uint64_t> mBindings;
            };
            eastl::vector<FArdaCudaLaunch> mLaunches;
            eastl::shared_ptr<FArdaCudaContext> mContext;
            CUstream mStream = nullptr;
            eastl::vector<eastl::shared_ptr<FArdaCudaModule>> mModules;
            bool mbSubmitted = false;
            bool mbFailed = false;
        };

        struct FArdaCudaContext final : IArdaCudaContext, eastl::enable_shared_from_this<FArdaCudaContext>
        {
            eastl::shared_ptr<FArdaCudaDriver> mDriver;
            eastl::shared_ptr<void> mNativeLifetime;
            CUcontext mContext = nullptr;
            FArdaCudaCapabilities mCapabilities;
            bool mbVulkan = false;
            mutable std::mutex mMutex;
            // CiG permits only one captured-but-unsubmitted batch per context in this backend.
            FArdaCudaBatch* mRecording = nullptr;
            std::unordered_map<std::string, eastl::shared_ptr<FArdaCudaModule>> mModuleCache;
            ~FArdaCudaContext() override
            {
                mModuleCache.clear();
                if (mContext) mDriver->cuCtxDestroy(mContext);
            }
            FArdaCudaCapabilities GetCapabilities() const override { return mCapabilities; }
            TArdaRHIResult<eastl::shared_ptr<IArdaCudaMapping>> ImportMemory(void*, uint64_t, uint64_t, const FArdaRHITextureDesc*) override;
            eastl::shared_ptr<IArdaCudaBatch> CreateBatch() override { return eastl::make_shared<FArdaCudaBatch>(shared_from_this()); }
        };

        FArdaCudaMapping::~FArdaCudaMapping()
        {
            auto& D = *mContext->mDriver;
            FArdaCudaScope Scope(D, mContext->mContext);
            if (Scope.mResult != CUDA_SUCCESS) return;
            // Views depend on arrays/memory: destroy in reverse construction order.
            for (auto Surface : mSurfaces) D.cuSurfObjectDestroy(Surface);
            if (mArray) D.cuMipmappedArrayDestroy(mArray);
            if (mBuffer) D.cuMemFree(mBuffer);
            if (mMemory) D.cuDestroyExternalMemory(mMemory);
        }

        TArdaRHIResult<eastl::shared_ptr<IArdaCudaMapping>> FArdaCudaContext::ImportMemory(
            void* Handle, uint64_t AllocationSize, uint64_t BufferSize, const FArdaRHITextureDesc* Texture)
        {
            auto Mapping = eastl::make_shared<FArdaCudaMapping>();
            Mapping->mContext = shared_from_this();
            auto& D = *mDriver;
            FArdaCudaScope Scope(D, mContext);
            if (auto S = D.Check(Scope.mResult, "Push CUDA context"); !S)
            {
#if !defined(_WIN32)
                close(static_cast<int>(reinterpret_cast<intptr_t>(Handle)));
#endif
                return {{}, S};
            }
            CUDA_EXTERNAL_MEMORY_HANDLE_DESC Import{};
#if defined(_WIN32)
            Import.type = mbVulkan ? CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 : CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
            Import.handle.win32.handle = Handle;
#else
            Import.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
            Import.handle.fd = static_cast<int>(reinterpret_cast<intptr_t>(Handle));
#endif
            Import.size = AllocationSize;
            Import.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;
            if (auto S = D.Check(D.cuImportExternalMemory(&Mapping->mMemory, &Import), "Import graphics allocation"); !S)
            {
#if !defined(_WIN32)
                close(Import.handle.fd); // CUDA consumes the FD only after a successful import.
#endif
                return {{}, S};
            }
            if (!Texture)
            {
                CUDA_EXTERNAL_MEMORY_BUFFER_DESC Range{};
                Range.size = BufferSize;
                if (auto S = D.Check(D.cuExternalMemoryGetMappedBuffer(&Mapping->mBuffer, Mapping->mMemory, &Range), "Map CUDA buffer"); !S) return {{}, S};
            }
            else
            {
                const auto Format = GetArdaCudaFormatInfo(Texture->mFormat);
                CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC Array{};
                Array.numLevels = Texture->mMipLevels;
                auto& A = Array.arrayDesc;
                A.Width = Texture->mWidth;
                A.Height = Texture->mDimension == EArdaRHITextureDimension::Texture1D ||
                    Texture->mDimension == EArdaRHITextureDimension::Texture1DArray ? 0 : Texture->mHeight;
                const bool Layered = Texture->mDimension == EArdaRHITextureDimension::Texture1DArray ||
                    Texture->mDimension == EArdaRHITextureDimension::Texture2DArray;
                A.Depth = Layered ? Texture->mArraySize :
                    (Texture->mDimension == EArdaRHITextureDimension::Texture3D ? Texture->mDepth : 0);
                A.NumChannels = Format.mChannels;
                A.Format = Format.mScalarType == EArdaCudaScalarType::Float
                    ? (Format.mBits == 16 ? CU_AD_FORMAT_HALF : CU_AD_FORMAT_FLOAT)
                    : (Format.mScalarType == EArdaCudaScalarType::UInt
                        ? (Format.mBits == 8 ? CU_AD_FORMAT_UNSIGNED_INT8 : Format.mBits == 16 ? CU_AD_FORMAT_UNSIGNED_INT16 : CU_AD_FORMAT_UNSIGNED_INT32)
                        : (Format.mBits == 8 ? CU_AD_FORMAT_SIGNED_INT8 : Format.mBits == 16 ? CU_AD_FORMAT_SIGNED_INT16 : CU_AD_FORMAT_SIGNED_INT32));
                A.Flags = CUDA_ARRAY3D_SURFACE_LDST | (Layered ? CUDA_ARRAY3D_LAYERED : 0);
                if (HasAnyFlags(Texture->mUsage, EArdaRHITextureUsage::RenderTarget)) A.Flags |= CUDA_ARRAY3D_COLOR_ATTACHMENT;
                if (auto S = D.Check(D.cuExternalMemoryGetMappedMipmappedArray(&Mapping->mArray, Mapping->mMemory, &Array), "Map CUDA image"); !S) return {{}, S};
                for (uint32_t Mip = 0; Mip < Texture->mMipLevels; ++Mip)
                {
                    CUDA_RESOURCE_DESC Resource{};
                    Resource.resType = CU_RESOURCE_TYPE_ARRAY;
                    if (auto S = D.Check(D.cuMipmappedArrayGetLevel(&Resource.res.array.hArray, Mapping->mArray, Mip), "Resolve CUDA mip"); !S) return {{}, S};
                    CUsurfObject Surface = 0;
                    if (auto S = D.Check(D.cuSurfObjectCreate(&Surface, &Resource), "Create CUDA surface"); !S) return {{}, S};
                    Mapping->mSurfaces.push_back(Surface);
                }
            }
            return {Mapping, {}};
        }

        FArdaCudaBatch::~FArdaCudaBatch()
        {
            std::lock_guard<std::mutex> Lock(mContext->mMutex);
            if (mContext->mRecording == this) mContext->mRecording = nullptr;
            FArdaCudaScope Scope(*mContext->mDriver, mContext->mContext);
            if (mStream && Scope.mResult == CUDA_SUCCESS) mContext->mDriver->cuStreamDestroy(mStream);
        }

        FArdaRHIStatus FArdaCudaBatch::ValidateSubmit() const
        {
            std::lock_guard<std::mutex> Lock(mContext->mMutex);
            if (mbSubmitted || mbFailed || (mContext->mRecording && mContext->mRecording != this))
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "CiG command lists are single-use and must submit in capture order; a failed capture must be discarded.");
            return {};
        }
        void FArdaCudaBatch::MarkSubmitted()
        {
            std::lock_guard<std::mutex> Lock(mContext->mMutex);
            mbSubmitted = true;
            if (mContext->mRecording == this) mContext->mRecording = nullptr;
        }

        FArdaRHIStatus FArdaCudaBatch::Record(void* CommandList, const eastl::vector<FArdaCudaKernel>& Kernels,
            const eastl::vector<uint64_t>& Bindings)
        {
            std::lock_guard<std::mutex> Lock(mContext->mMutex);
            if (mbSubmitted || mbFailed || (mContext->mRecording && mContext->mRecording != this))
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "Submit or discard the previous CiG command list before recording another one on this context.");
            auto& D = *mContext->mDriver;
            FArdaCudaScope Scope(D, mContext->mContext);
            if (auto S = D.Check(Scope.mResult, "Push CiG context"); !S) return S;
            if (!mStream)
                if (auto S = D.Check(D.cuStreamCreate(&mStream, CU_STREAM_NON_BLOCKING), "Create CiG stream"); !S) return S;
            eastl::vector<CUfunction> Functions;
            // Validate every entry point and argument before beginning native capture.
            for (const auto& K : Kernels)
            {
                const std::string Key(K.mPtx.data(), K.mPtx.size());
                auto Found = mContext->mModuleCache.find(Key);
                eastl::shared_ptr<FArdaCudaModule> Module;
                if (Found != mContext->mModuleCache.end()) Module = Found->second;
                else
                {
                    Module = eastl::make_shared<FArdaCudaModule>();
                    Module->mDriver = mContext->mDriver;
                    Module->mContext = mContext->mContext;
                    if (auto S = D.Check(D.cuModuleLoadDataEx(&Module->mModule, K.mPtx.c_str(), 0, nullptr, nullptr), "Compile CUDA PTX"); !S) return S;
                    // Cache eviction drops only the lookup reference. Recorded batches
                    // retain their executable modules until the graphics work completes.
                    if (mContext->mModuleCache.size() >= 64) mContext->mModuleCache.clear();
                    mContext->mModuleCache.emplace(Key, Module);
                }
                CUfunction Function = nullptr;
                if (auto S = D.Check(D.cuModuleGetFunction(&Function, Module->mModule, K.mEntryPoint.c_str()), "Find CUDA kernel"); !S) return S;
                int StaticShared = 0, MaxThreads = 0;
                if (auto Status = D.Check(D.cuFuncGetAttribute(&StaticShared, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, Function),
                    "Query CUDA kernel shared memory"); !Status) return Status;
                if (auto Status = D.Check(D.cuFuncGetAttribute(&MaxThreads, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, Function),
                    "Query CUDA kernel thread limit"); !Status) return Status;
                if (uint64_t(K.mBlockSize[0]) * K.mBlockSize[1] * K.mBlockSize[2] > uint32_t(MaxThreads) ||
                    uint64_t(StaticShared) + K.mSharedMemoryBytes > mContext->mCapabilities.mMaxSharedMemoryBytes)
                    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA kernel exceeds its thread/shared-memory limit.");
                for (size_t I = 0; D.cuFuncGetParamInfo && I <= K.mArguments.size(); ++I)
                {
                    size_t Offset = 0, Size = 0;
                    const auto Result = D.cuFuncGetParamInfo(Function, I, &Offset, &Size);
                    if (I == K.mArguments.size() ? Result != CUDA_ERROR_INVALID_VALUE :
                        Result != CUDA_SUCCESS || Size != (K.mArguments[I].mBindingIndex == UINT32_MAX ? K.mArguments[I].mValue.size() : sizeof(uint64_t)))
                        return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA kernel argument count or byte size does not match PTX metadata.");
                }
                Functions.push_back(Function);
                mModules.push_back(eastl::move(Module));
            }
            if (mContext->mCapabilities.mLaunchMode == EArdaCudaLaunchMode::ContextSwitch)
            {
                mLaunches.push_back({Kernels, eastl::move(Functions), Bindings});
                return {};
            }
#if CUDA_VERSION >= 13030
            CUstreamCigParam Native{STREAM_CIG_DATA_TYPE_D3D12_COMMAND_LIST, CommandList};
            CUstreamCigCaptureParams Capture{&Native};
            if (auto S = D.Check(D.cuStreamBeginCaptureToCig(mStream, &Capture), "Begin CiG capture"); !S) return S;
            mContext->mRecording = this;
            auto Status = Launch(Kernels, Functions, Bindings);
            auto End = D.Check(D.cuStreamEndCaptureToCig(mStream), "End CiG capture");
            if (Status && !End) Status = End;
            mbFailed = !Status;
            return Status;
#else
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "This SDK does not declare CiG stream capture.");
#endif
        }

        FArdaRHIStatus FArdaCudaBatch::Launch(const eastl::vector<FArdaCudaKernel>& Kernels,
            const eastl::vector<CUfunction>& Functions, const eastl::vector<uint64_t>& Bindings)
        {
            auto& D = *mContext->mDriver;
            for (size_t I = 0; I < Kernels.size(); ++I)
            {
                const auto& K = Kernels[I];
                eastl::vector<void*> Arguments;
                for (const auto& A : K.mArguments)
                    Arguments.push_back(A.mBindingIndex == UINT32_MAX ? static_cast<void*>(const_cast<uint8_t*>(A.mValue.data())) :
                        static_cast<void*>(const_cast<uint64_t*>(&Bindings[A.mBindingIndex])));
                auto Status = D.Check(D.cuLaunchKernel(Functions[I], K.mGridSize[0], K.mGridSize[1], K.mGridSize[2],
                    K.mBlockSize[0], K.mBlockSize[1], K.mBlockSize[2], K.mSharedMemoryBytes,
                    mStream, Arguments.data(), nullptr), "Launch CUDA kernel");
                if (!Status) return Status;
            }
            return {};
        }

        FArdaRHIStatus FArdaCudaBatch::Execute()
        {
            std::lock_guard<std::mutex> Lock(mContext->mMutex);
            if (mbSubmitted || mbFailed || mContext->mCapabilities.mLaunchMode != EArdaCudaLaunchMode::ContextSwitch)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "CUDA batch cannot be executed or replayed.");
            auto& D = *mContext->mDriver;
            FArdaCudaScope Scope(D, mContext->mContext);
            if (auto S = D.Check(Scope.mResult, "Push CUDA context"); !S) return S;
            FArdaRHIStatus Status;
            for (const auto& LaunchInfo : mLaunches)
            {
                Status = Launch(LaunchInfo.mKernels, LaunchInfo.mFunctions, LaunchInfo.mBindings);
                if (!Status) break;
            }
            // Drain even a partially failed launch before allowing resource destruction.
            auto Completion = D.Check(D.cuStreamSynchronize(mStream), "Complete CUDA segment");
            if (Status && !Completion) Status = Completion;
            mbSubmitted = true;
            mbFailed = !Status;
            return Status;
        }
    }

    static TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateContext(void* Queue, const void* Identifier, eastl::shared_ptr<void> Lifetime, EArdaCudaExecutionMode Mode, bool bVulkan)
    {
        const auto Unsupported = [](const char* Reason) -> TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>>
        { return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Reason)}; };
        auto D = eastl::make_shared<FArdaCudaDriver>();
        if (!D->IsComplete()) return Unsupported("CUDA driver or required external-memory entry points are unavailable.");
        if (auto S = D->Check(D->cuInit(0), "Initialize CUDA driver"); !S) return {{}, S};
        int Count = 0;
        if (auto S = D->Check(D->cuDeviceGetCount(&Count), "Enumerate CUDA devices"); !S) return {{}, S};
        CUdevice Device = -1;
        for (int I = 0; I < Count; ++I)
        {
            CUdevice CandidateDevice;
            if (D->cuDeviceGet(&CandidateDevice, I) != CUDA_SUCCESS) continue;
            if (bVulkan)
            {
                CUuuid Uuid{};
                if (D->cuDeviceGetUuid(&Uuid, CandidateDevice) == CUDA_SUCCESS && !std::memcmp(Uuid.bytes, Identifier, sizeof(Uuid.bytes)))
                { Device = CandidateDevice; break; }
            }
            else
            {
                char Candidate[8]{}; unsigned int Nodes = 0;
                if (D->cuDeviceGetLuid && D->cuDeviceGetLuid(Candidate, &Nodes, CandidateDevice) == CUDA_SUCCESS && Nodes == 1 && !std::memcmp(Candidate, Identifier, 8))
                { Device = CandidateDevice; break; }
            }
        }
        if (Device < 0) return Unsupported("No CUDA adapter matches the graphics device identity.");
        const auto Attribute = [&](CUdevice_attribute A) { int Value = 0; D->cuDeviceGetAttribute(&Value, A, Device); return uint32_t(Value); };
        auto Context = eastl::make_shared<FArdaCudaContext>();
        Context->mDriver = D;
        Context->mbVulkan = bVulkan;
        Context->mNativeLifetime = eastl::move(Lifetime);
        auto& C = Context->mCapabilities;
        C.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
        if (Mode != EArdaCudaExecutionMode::ContextSwitch)
        {
            C.mFallbackReason = "D3D12 CiG stream capture is unavailable in this SDK, driver, or adapter.";
#if CUDA_VERSION >= 13030
            if (D->cuCtxCreate && D->cuStreamBeginCaptureToCig && D->cuStreamEndCaptureToCig &&
                Attribute(CU_DEVICE_ATTRIBUTE_D3D12_CIG_SUPPORTED) && Attribute(CU_DEVICE_ATTRIBUTE_D3D12_CIG_STREAMS_SUPPORTED))
            {
                CUctxCigParam Cig{CIG_DATA_TYPE_D3D12_COMMAND_QUEUE, Queue};
                CUctxCreateParams Params{}; Params.cigParams = &Cig;
                auto Status = D->Check(D->cuCtxCreate(&Context->mContext, &Params, 0, Device), "Create D3D12 CiG context");
                if (Status)
                {
                    C.mLaunchMode = EArdaCudaLaunchMode::D3D12CiG;
                    C.mFallbackReason.clear();
                }
                else C.mFallbackReason = Status.mMessage;
            }
#endif
            if (!Context->mContext && Mode == EArdaCudaExecutionMode::GraphicsQueue)
                return Unsupported(C.mFallbackReason.c_str());
        }
        if (!Context->mContext)
            if (auto Status = D->Check(D->mCreateContext(&Context->mContext, 0, Device), "Create ordinary CUDA context"); !Status)
                return {{}, Status};
        CUcontext Popped = nullptr;
        if (auto Status = D->Check(D->cuCtxPopCurrent(&Popped), "Restore caller CUDA context"); !Status) return {{}, Status};
        C.mComputeCapability = Attribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR) * 10 + Attribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);
        C.mMaxThreadsPerBlock = Attribute(CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK);
        for (uint32_t I = 0; I < 3; ++I)
        {
            C.mMaxBlockSize[I] = Attribute(static_cast<CUdevice_attribute>(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X + I));
            C.mMaxGridSize[I] = Attribute(static_cast<CUdevice_attribute>(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X + I));
        }
        C.mMaxSharedMemoryBytes = Attribute(CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK);
#if CUDA_VERSION >= 13030
        if (C.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG)
        {
            FArdaCudaScope Scope(*D, Context->mContext);
            if (auto Status = D->Check(Scope.mResult, "Push CiG context"); !Status) return {{}, Status};
            size_t Shared = 0;
            if (auto Status = D->Check(D->cuCtxGetLimit(&Shared, CU_LIMIT_SHMEM_SIZE), "Query CiG shared-memory limit"); !Status) return {{}, Status};
            C.mMaxSharedMemoryBytes = static_cast<uint32_t>(eastl::min<size_t>(Shared, C.mMaxSharedMemoryBytes));
        }
#endif
        C.mbSurfaceAccess = true;
        // Opaque Vulkan layered images have not passed graphics/CUDA layer-stride
        // qualification. Do not expose a successfully mapped but incompatible layout.
        C.mbLayeredSurfaceAccess = !bVulkan;
        C.mUnavailableReason.clear();
        return {Context, {}};
    }
    TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaD3D12CudaContext(void* Queue, const void* Luid,
        eastl::shared_ptr<void> Lifetime, EArdaCudaExecutionMode Mode)
    { return CreateContext(Queue, Luid, eastl::move(Lifetime), Mode, false); }

    TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaVulkanCudaContext(const void* DeviceUuid)
    { return CreateContext(nullptr, DeviceUuid, {}, EArdaCudaExecutionMode::ContextSwitch, true); }
}
#else
namespace arda
{
    TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaD3D12CudaContext(void*, const void*, eastl::shared_ptr<void>, EArdaCudaExecutionMode)
    { return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "This build excludes the CUDA driver provider.")}; }
    TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaVulkanCudaContext(const void*)
    { return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "This build excludes the CUDA driver provider.")}; }
}
#endif
