/** @file ArdaCudaModule.h
 * Owns reusable CUDA source independently of invocation arguments and launch geometry.
 */
#pragma once

#include "RHI/ArdaRHITypes.h"

#include <EASTL/functional.h>
#include <EASTL/unique_ptr.h>

namespace arda
{
    /** Language accepted by a CUDA source module. */
    enum class EArdaCudaSourceLanguage : uint8_t
    {
        /** CUDA C++ translated to PTX by the module's compiler callback. */
        CudaCpp,
        /** Precompiled PTX passed directly to the native launch provider. */
        Ptx
    };

    /** An owned include supplied to the compiler without filesystem dependencies. */
    struct FArdaCudaSourceHeader
    {
        /** Unique include name used by #include in the CUDA C++ source. */
        eastl::string mName;
        /** Header contents; embedded NULs are rejected. */
        eastl::string mCode;
    };

    /** Immutable translation unit shared by any number of operand implementations. */
    struct FArdaCudaModuleSource
    {
        /** Source filename used for compiler diagnostics, for example ArdaMatmul.cu. */
        eastl::string mName;
        /** Owned source contents, loaded from a .cu/.ptx file or embedded by the application. */
        eastl::string mCode;
        /** CudaCpp requires an explicit compiler; Ptx requires no CUDA compiler or SDK. */
        EArdaCudaSourceLanguage mLanguage = EArdaCudaSourceLanguage::CudaCpp;
        /** CUDA C++ includes, supplied in declaration order to the compiler callback. */
        eastl::vector<FArdaCudaSourceHeader> mHeaders;
        /** CUDA C++ compiler options; the callback must honor these and the requested SM target. */
        eastl::vector<eastl::string> mCompileOptions;
    };

    /** Translates a source unit to owned PTX without a trailing NUL, or returns compiler diagnostics.
     * The target is major * 10 + minor. Use an NVRTC adapter or an offline artifact lookup.
     * Results must depend only on the immutable source/options and target; resources are unavailable.
     * Calls are serialized per module. A compiler shared by several modules must support concurrency.
     */
    using FArdaCudaModuleCompiler = eastl::function<TArdaRHIResult<eastl::string>(
        const FArdaCudaModuleSource&, uint32_t)>;

    /** Reusable source and thread-safe PTX cache, with no native CUDA context ownership. */
    class FArdaCudaModule final
    {
    public:
        /** Validates and owns source/compiler without compiling or requiring a CUDA device.
         * CudaCpp requires a compiler; Ptx rejects compiler callbacks, headers and options.
         * Reference captures must outlive the module; callbacks must not reenter this module.
         */
        [[nodiscard]] static TArdaRHIResult<eastl::shared_ptr<const FArdaCudaModule>> Create(
            FArdaCudaModuleSource Source, FArdaCudaModuleCompiler Compiler = {});
        /** Releases source, compiler and cached PTX after all retained module references are released. */
        ~FArdaCudaModule();
        /** Returns the immutable source, includes and compilation options. */
        [[nodiscard]] const FArdaCudaModuleSource& GetSource() const noexcept;
        /** Returns PTX for a nonzero SM target; successful C++ compilations are cached per SM.
         * Failures and invalid compiler output are propagated without caching, allowing retry.
         * This prepares code only; native module loading and GPU execution belong to the provider.
         */
        [[nodiscard]] TArdaRHIResult<eastl::string> GetPtx(uint32_t ComputeCapability) const;

    private:
        struct FArdaState;
        FArdaCudaModule(FArdaCudaModuleSource Source, FArdaCudaModuleCompiler Compiler);
        eastl::unique_ptr<FArdaState> mState;
    };
}
