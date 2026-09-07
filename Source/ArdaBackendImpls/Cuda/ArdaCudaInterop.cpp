/** @file ArdaCudaInterop.cpp
 * Loads the CUDA driver dynamically and captures PTX launches into D3D12 CiG.
 * Imported mappings, cached modules and capture streams follow graphics-fence lifetime.
 */
#include "ArdaCudaInterop.h"

#if defined(ARDA_ENABLE_CUDA) && defined(_WIN32)
#include <cuda.h>
#if CUDA_VERSION < 13030
#error Ardashir D3D12 CiG requires CUDA 13.3 or newer headers.
#endif
#include <Windows.h>
#include <EASTL/algorithm.h>
#include <EASTL/shared_ptr.h>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <string>

namespace arda::backend::cuda
{
    namespace
    {
        // Resolve versioned driver entry points without creating a mandatory CUDA DLL import.
        struct FDriver
        {
            HMODULE mLibrary = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#define ARDA_CUDA_FUNCTION(Name, Symbol) decltype(&::Name) Name = mLibrary ? reinterpret_cast<decltype(Name)>(GetProcAddress(mLibrary, Symbol)) : nullptr
            ARDA_CUDA_FUNCTION(cuInit, "cuInit");
            ARDA_CUDA_FUNCTION(cuDeviceGetCount, "cuDeviceGetCount");
            ARDA_CUDA_FUNCTION(cuDeviceGet, "cuDeviceGet");
            ARDA_CUDA_FUNCTION(cuDeviceGetLuid, "cuDeviceGetLuid");
            ARDA_CUDA_FUNCTION(cuDeviceGetAttribute, "cuDeviceGetAttribute");
            ARDA_CUDA_FUNCTION(cuCtxCreate, "cuCtxCreate_v4");
            ARDA_CUDA_FUNCTION(cuCtxDestroy, "cuCtxDestroy_v2");
            ARDA_CUDA_FUNCTION(cuCtxPushCurrent, "cuCtxPushCurrent_v2");
            ARDA_CUDA_FUNCTION(cuCtxPopCurrent, "cuCtxPopCurrent_v2");
            ARDA_CUDA_FUNCTION(cuCtxGetLimit, "cuCtxGetLimit");
            ARDA_CUDA_FUNCTION(cuStreamCreate, "cuStreamCreate");
            ARDA_CUDA_FUNCTION(cuStreamDestroy, "cuStreamDestroy_v2");
            ARDA_CUDA_FUNCTION(cuStreamBeginCaptureToCig, "cuStreamBeginCaptureToCig");
            ARDA_CUDA_FUNCTION(cuStreamEndCaptureToCig, "cuStreamEndCaptureToCig");
            ARDA_CUDA_FUNCTION(cuModuleLoadDataEx, "cuModuleLoadDataEx");
            ARDA_CUDA_FUNCTION(cuModuleUnload, "cuModuleUnload");
            ARDA_CUDA_FUNCTION(cuModuleGetFunction, "cuModuleGetFunction");
            ARDA_CUDA_FUNCTION(cuFuncGetParamInfo, "cuFuncGetParamInfo");
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
            ~FDriver() { if (mLibrary) FreeLibrary(mLibrary); }
            bool IsComplete() const
            {
                return cuInit && cuDeviceGetCount && cuDeviceGet && cuDeviceGetLuid && cuDeviceGetAttribute &&
                    cuCtxCreate && cuCtxDestroy && cuCtxPushCurrent && cuCtxPopCurrent && cuCtxGetLimit &&
                    cuStreamCreate && cuStreamDestroy && cuStreamBeginCaptureToCig && cuStreamEndCaptureToCig &&
                    cuModuleLoadDataEx && cuModuleUnload && cuModuleGetFunction && cuFuncGetParamInfo && cuFuncGetAttribute &&
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
        struct FScope
        {
            FDriver& mDriver;
            CUresult mResult;
            FScope(FDriver& Driver, CUcontext Context) : mDriver(Driver), mResult(Driver.cuCtxPushCurrent(Context)) {}
            ~FScope() { if (mResult == CUDA_SUCCESS) { CUcontext Previous; mDriver.cuCtxPopCurrent(&Previous); } }
        };

        struct FContext;
        // The mapping retains its context; the native resource owns the mapping and outlives it.
        struct FMapping final : IMapping
        {
            eastl::shared_ptr<FContext> mContext;
            CUexternalMemory mMemory = nullptr;
            CUdeviceptr mBuffer = 0;
            CUmipmappedArray mArray = nullptr;
            eastl::vector<CUsurfObject> mSurfaces;
            ~FMapping() override;
            uint64_t GetArgument(uint32_t Mip, uint64_t Offset) const override
            { return mBuffer ? mBuffer + Offset : mSurfaces.at(Mip); }
        };

        struct FModule
        {
            eastl::shared_ptr<FDriver> mDriver;
            CUcontext mContext = nullptr;
            CUmodule mModule = nullptr;
            ~FModule()
            {
                FScope Scope(*mDriver, mContext);
                if (Scope.mResult == CUDA_SUCCESS && mModule) mDriver->cuModuleUnload(mModule);
            }
        };

        // Stream and module references survive recording and are retired with the submitted list.
        struct FBatch final : IBatch
        {
            explicit FBatch(eastl::shared_ptr<FContext> Context) : mContext(eastl::move(Context)) {}
            ~FBatch() override;
            const void* GetIdentity() const noexcept override { return this; }
            FArdaRHIStatus Record(void*, const eastl::vector<FArdaCudaKernel>&, const eastl::vector<uint64_t>&) override;
            FArdaRHIStatus ValidateSubmit() const override;
            void MarkSubmitted() override;
            eastl::shared_ptr<FContext> mContext;
            CUstream mStream = nullptr;
            eastl::vector<eastl::shared_ptr<FModule>> mModules;
            bool mbSubmitted = false;
            bool mbFailed = false;
        };

        struct FContext final : IContext, eastl::enable_shared_from_this<FContext>
        {
            eastl::shared_ptr<FDriver> mDriver;
            eastl::shared_ptr<void> mNativeLifetime;
            CUcontext mContext = nullptr;
            FArdaCudaCapabilities mCapabilities;
            mutable std::mutex mMutex;
            // CiG permits only one captured-but-unsubmitted batch per context in this backend.
            FBatch* mRecording = nullptr;
            std::unordered_map<std::string, eastl::shared_ptr<FModule>> mModuleCache;
            ~FContext() override
            {
                mModuleCache.clear();
                if (mContext) mDriver->cuCtxDestroy(mContext);
            }
            FArdaCudaCapabilities GetCapabilities() const override { return mCapabilities; }
            TArdaRHIResult<eastl::shared_ptr<IMapping>> ImportMemory(void*, uint64_t, uint64_t, const FArdaRHITextureDesc*) override;
            eastl::shared_ptr<IBatch> CreateBatch() override { return eastl::make_shared<FBatch>(shared_from_this()); }
        };

        FMapping::~FMapping()
        {
            auto& D = *mContext->mDriver;
            FScope Scope(D, mContext->mContext);
            if (Scope.mResult != CUDA_SUCCESS) return;
            // Views depend on arrays/memory: destroy in reverse construction order.
            for (auto Surface : mSurfaces) D.cuSurfObjectDestroy(Surface);
            if (mArray) D.cuMipmappedArrayDestroy(mArray);
            if (mBuffer) D.cuMemFree(mBuffer);
            if (mMemory) D.cuDestroyExternalMemory(mMemory);
        }

        TArdaRHIResult<eastl::shared_ptr<IMapping>> FContext::ImportMemory(
            void* Handle, uint64_t AllocationSize, uint64_t BufferSize, const FArdaRHITextureDesc* Texture)
        {
            auto Mapping = eastl::make_shared<FMapping>();
            Mapping->mContext = shared_from_this();
            auto& D = *mDriver;
            FScope Scope(D, mContext);
            if (auto S = D.Check(Scope.mResult, "Push CUDA context"); !S) return {{}, S};
            CUDA_EXTERNAL_MEMORY_HANDLE_DESC Import{};
            Import.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
            Import.handle.win32.handle = Handle;
            Import.size = AllocationSize;
            Import.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;
            if (auto S = D.Check(D.cuImportExternalMemory(&Mapping->mMemory, &Import), "Import D3D12 allocation"); !S) return {{}, S};
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

        FBatch::~FBatch()
        {
            std::lock_guard<std::mutex> Lock(mContext->mMutex);
            if (mContext->mRecording == this) mContext->mRecording = nullptr;
            FScope Scope(*mContext->mDriver, mContext->mContext);
            if (mStream && Scope.mResult == CUDA_SUCCESS) mContext->mDriver->cuStreamDestroy(mStream);
        }

        FArdaRHIStatus FBatch::ValidateSubmit() const
        {
            std::lock_guard<std::mutex> Lock(mContext->mMutex);
            if (mbSubmitted || mbFailed || (mContext->mRecording && mContext->mRecording != this))
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "CiG command lists are single-use and must submit in capture order; a failed capture must be discarded.");
            return {};
        }
        void FBatch::MarkSubmitted()
        {
            std::lock_guard<std::mutex> Lock(mContext->mMutex);
            mbSubmitted = true;
            if (mContext->mRecording == this) mContext->mRecording = nullptr;
        }

        FArdaRHIStatus FBatch::Record(void* CommandList, const eastl::vector<FArdaCudaKernel>& Kernels,
            const eastl::vector<uint64_t>& Bindings)
        {
            std::lock_guard<std::mutex> Lock(mContext->mMutex);
            if (mbSubmitted || mbFailed || (mContext->mRecording && mContext->mRecording != this))
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "Submit or discard the previous CiG command list before recording another one on this context.");
            auto& D = *mContext->mDriver;
            FScope Scope(D, mContext->mContext);
            if (auto S = D.Check(Scope.mResult, "Push CiG context"); !S) return S;
            if (!mStream)
                if (auto S = D.Check(D.cuStreamCreate(&mStream, CU_STREAM_NON_BLOCKING), "Create CiG stream"); !S) return S;
            eastl::vector<CUfunction> Functions;
            // Validate every entry point and argument before beginning native capture.
            for (const auto& K : Kernels)
            {
                const std::string Key(K.mPtx.data(), K.mPtx.size());
                auto Found = mContext->mModuleCache.find(Key);
                eastl::shared_ptr<FModule> Module;
                if (Found != mContext->mModuleCache.end()) Module = Found->second;
                else
                {
                    Module = eastl::make_shared<FModule>();
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
                D.cuFuncGetAttribute(&StaticShared, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, Function);
                D.cuFuncGetAttribute(&MaxThreads, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, Function);
                if (uint64_t(K.mBlockSize[0]) * K.mBlockSize[1] * K.mBlockSize[2] > uint32_t(MaxThreads) ||
                    uint64_t(StaticShared) + K.mSharedMemoryBytes > mContext->mCapabilities.mMaxSharedMemoryBytes)
                    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA kernel exceeds its thread/shared-memory limit.");
                for (size_t I = 0; I <= K.mArguments.size(); ++I)
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
            CUstreamCigParam Native{STREAM_CIG_DATA_TYPE_D3D12_COMMAND_LIST, CommandList};
            CUstreamCigCaptureParams Capture{&Native};
            if (auto S = D.Check(D.cuStreamBeginCaptureToCig(mStream, &Capture), "Begin CiG capture"); !S) return S;
            mContext->mRecording = this;
            FArdaRHIStatus Status;
            for (size_t I = 0; I < Kernels.size(); ++I)
            {
                const auto& K = Kernels[I];
                eastl::vector<void*> Arguments;
                for (const auto& A : K.mArguments)
                    Arguments.push_back(A.mBindingIndex == UINT32_MAX ? static_cast<void*>(const_cast<uint8_t*>(A.mValue.data())) :
                        static_cast<void*>(const_cast<uint64_t*>(&Bindings[A.mBindingIndex])));
                Status = D.Check(D.cuLaunchKernel(Functions[I], K.mGridSize[0], K.mGridSize[1], K.mGridSize[2],
                    K.mBlockSize[0], K.mBlockSize[1], K.mBlockSize[2], K.mSharedMemoryBytes,
                    mStream, Arguments.data(), nullptr), "Record CUDA kernel");
                if (!Status) break;
            }
            auto End = D.Check(D.cuStreamEndCaptureToCig(mStream), "End CiG capture");
            if (Status && !End) Status = End;
            mbFailed = !Status;
            return Status;
        }
    }

    TArdaRHIResult<eastl::shared_ptr<IContext>> CreateD3D12Context(void* Queue, const void* Luid, eastl::shared_ptr<void> Lifetime)
    {
        const auto Unsupported = [](const char* Reason) -> TArdaRHIResult<eastl::shared_ptr<IContext>>
        { return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Reason)}; };
        auto D = eastl::make_shared<FDriver>();
        if (!D->IsComplete()) return Unsupported("CUDA driver or required CiG entry points are unavailable.");
        if (auto S = D->Check(D->cuInit(0), "Initialize CUDA driver"); !S) return {{}, S};
        int Count = 0;
        if (auto S = D->Check(D->cuDeviceGetCount(&Count), "Enumerate CUDA devices"); !S) return {{}, S};
        CUdevice Device = -1;
        for (int I = 0; I < Count; ++I)
        {
            CUdevice CandidateDevice;
            if (D->cuDeviceGet(&CandidateDevice, I) != CUDA_SUCCESS) continue;
            char Candidate[8]{}; unsigned int Nodes = 0;
            if (D->cuDeviceGetLuid(Candidate, &Nodes, CandidateDevice) == CUDA_SUCCESS && Nodes == 1 && !std::memcmp(Candidate, Luid, 8))
            { Device = CandidateDevice; break; }
        }
        if (Device < 0) return Unsupported("No single-node CUDA adapter matches the D3D12 device LUID.");
        const auto Attribute = [&](CUdevice_attribute A) { int Value = 0; D->cuDeviceGetAttribute(&Value, A, Device); return uint32_t(Value); };
        if (!Attribute(CU_DEVICE_ATTRIBUTE_D3D12_CIG_SUPPORTED) || !Attribute(CU_DEVICE_ATTRIBUTE_D3D12_CIG_STREAMS_SUPPORTED))
            return Unsupported("The rendering adapter does not support D3D12 CiG command-list capture.");
        auto Context = eastl::make_shared<FContext>();
        Context->mDriver = D;
        Context->mNativeLifetime = eastl::move(Lifetime);
        CUctxCigParam Cig{CIG_DATA_TYPE_D3D12_COMMAND_QUEUE, Queue};
        CUctxCreateParams Params{}; Params.cigParams = &Cig;
        if (auto S = D->Check(D->cuCtxCreate(&Context->mContext, &Params, 0, Device), "Create D3D12 CiG context"); !S) return {{}, S};
        CUcontext Popped = nullptr;
        if (auto S = D->Check(D->cuCtxPopCurrent(&Popped), "Restore caller CUDA context"); !S) return {{}, S};
        auto& C = Context->mCapabilities;
        C.mLaunchMode = EArdaCudaLaunchMode::D3D12CiG;
        C.mComputeCapability = Attribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR) * 10 + Attribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);
        C.mMaxThreadsPerBlock = Attribute(CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK);
        for (uint32_t I = 0; I < 3; ++I)
        {
            C.mMaxBlockSize[I] = Attribute(static_cast<CUdevice_attribute>(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X + I));
            C.mMaxGridSize[I] = Attribute(static_cast<CUdevice_attribute>(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X + I));
        }
        FScope Scope(*D, Context->mContext);
        size_t Shared = 0;
        if (auto S = D->Check(D->cuCtxGetLimit(&Shared, CU_LIMIT_SHMEM_SIZE), "Query CiG shared-memory limit"); !S) return {{}, S};
        C.mMaxSharedMemoryBytes = static_cast<uint32_t>(eastl::min<size_t>(Shared, Attribute(CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK)));
        C.mbSurfaceAccess = true;
        C.mUnavailableReason.clear();
        return {Context, {}};
    }
}
#else
namespace arda::backend::cuda
{
    TArdaRHIResult<eastl::shared_ptr<IContext>> CreateD3D12Context(void*, const void*, eastl::shared_ptr<void>)
    { return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "This build excludes the CUDA CiG provider.")}; }
}
#endif
