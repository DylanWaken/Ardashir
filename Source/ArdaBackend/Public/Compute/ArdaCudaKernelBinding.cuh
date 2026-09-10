/** Include from nvcc-compiled registration units only. C++ consumers require no CUDA SDK. */
#pragma once
#include "ArdaCudaKernelVariants.h"
#include <cuda_runtime.h>

namespace arda
{
    struct FArdaCudaBindingHelpers
    {
        template<class T> struct Signature { static FArdaCudaKernelSignature Get() { return {}; } };
        template<class P> struct Signature<void(*)(P)>
        {
            static FArdaCudaKernelSignature Get()
            { return {&typeid(P), sizeof(P), alignof(P), !std::is_reference_v<P> &&
                std::is_trivially_copyable_v<P> && std::is_standard_layout_v<P>}; }
        };
        static FArdaRHIStatus Check(cudaError_t Error)
        { return Error == cudaSuccess ? FArdaRHIStatus{} : FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, cudaGetErrorString(Error)); }
        template<auto Kernel> class Entry final : public IArdaCudaKernelEntry
        {
        public:
            explicit Entry(FArdaCudaBuildInfo Build) : mBuild(eastl::move(Build)) {}
            FArdaCudaKernelSignature GetSignature() const noexcept override { return Signature<decltype(Kernel)>::Get(); }
            const FArdaCudaBuildInfo& GetBuildInfo() const noexcept override { return mBuild; }
            TArdaRHIResult<FArdaCudaKernelLimits> GetLimits() const override
            {
                if (!GetSignature().mbSupported) return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA kernel requires one plain parameter struct by value.")};
                cudaFuncAttributes A{};
                if (auto S = Check(cudaFuncGetAttributes(&A, reinterpret_cast<const void*>(Kernel))); !S) return {{}, S};
                return {{uint32_t(A.maxThreadsPerBlock), uint32_t(A.sharedSizeBytes), uint32_t(A.maxDynamicSharedSizeBytes)}, {}};
            }
            FArdaRHIStatus Launch(void* Stream, const FArdaCudaLaunchConfig& C, const void* Parameters, size_t Size) const override
            {
                const auto S = GetSignature();
                if (!S.mbSupported || Size != S.mSize || !Parameters || !Stream)
                    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Invalid CUDA compiled-kernel invocation.");
                void* Arguments[] = {const_cast<void*>(Parameters)};
                return Check(cudaLaunchKernel(reinterpret_cast<const void*>(Kernel),
                    dim3(C.mGridSize[0], C.mGridSize[1], C.mGridSize[2]),
                    dim3(C.mBlockSize[0], C.mBlockSize[1], C.mBlockSize[2]), Arguments,
                    C.mSharedMemoryBytes, static_cast<cudaStream_t>(Stream)));
            }
        private:
            const FArdaCudaBuildInfo mBuild;
        };
    };
    /** Binds a precompiled symbol. Registry initialization reports incompatible signatures at runtime. */
    template<auto Kernel, class Payload>
    TArdaCudaKernelVariant<Kernel, Payload> BindArdaCudaKernel(const char* Name, Payload Info,
        FArdaCudaBuildInfo Build, FArdaCudaKernelRequirements Requirements = {})
    {
        return {eastl::make_shared<FArdaCudaBindingHelpers::Entry<Kernel>>(eastl::move(Build)),
            eastl::move(Info), Name, Requirements};
    }
}
