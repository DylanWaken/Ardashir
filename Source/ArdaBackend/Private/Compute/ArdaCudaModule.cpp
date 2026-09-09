#include "Compute/ArdaCudaModule.h"

#include <EASTL/algorithm.h>
#include <EASTL/map.h>

#include <mutex>

namespace arda
{
    struct FArdaCudaModule::FArdaState
    {
        FArdaCudaModuleSource mSource;
        FArdaCudaModuleCompiler mCompiler;
        std::mutex mMutex;
        eastl::map<uint32_t, eastl::string> mPtxByArchitecture;
    };

    FArdaCudaModule::FArdaCudaModule(FArdaCudaModuleSource Source, FArdaCudaModuleCompiler Compiler)
        : mState(new FArdaState)
    {
        mState->mSource = eastl::move(Source);
        mState->mCompiler = eastl::move(Compiler);
    }

    FArdaCudaModule::~FArdaCudaModule() = default;

    TArdaRHIResult<eastl::shared_ptr<const FArdaCudaModule>> FArdaCudaModule::Create(
        FArdaCudaModuleSource Source, FArdaCudaModuleCompiler Compiler)
    {
        const auto IsText = [](const eastl::string& Text)
        {
            return !Text.empty() && Text.find('\0') == eastl::string::npos;
        };
        if (!IsText(Source.mName) || !IsText(Source.mCode) ||
            Source.mLanguage > EArdaCudaSourceLanguage::Ptx ||
            (Source.mLanguage == EArdaCudaSourceLanguage::CudaCpp && !Compiler) ||
            (Source.mLanguage == EArdaCudaSourceLanguage::Ptx &&
                (Compiler || !Source.mHeaders.empty() || !Source.mCompileOptions.empty())))
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                "CUDA module requires valid source and a compiler matching its source language.")};
        }
        for (size_t Index = 0; Index < Source.mHeaders.size(); ++Index)
        {
            const auto& Header = Source.mHeaders[Index];
            if (!IsText(Header.mName) || Header.mName == Source.mName ||
                Header.mCode.find('\0') != eastl::string::npos ||
                eastl::any_of(Source.mHeaders.begin(), Source.mHeaders.begin() + Index,
                    [&](const auto& Other) { return Header.mName == Other.mName; }))
            {
                return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "CUDA include names must be unique and source text must not contain NULs.")};
            }
        }
        for (const auto& Option : Source.mCompileOptions)
        {
            if (!IsText(Option))
            {
                return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "CUDA compiler options must be nonempty and must not contain NULs.")};
            }
        }
        return {eastl::shared_ptr<const FArdaCudaModule>(
            new FArdaCudaModule(eastl::move(Source), eastl::move(Compiler))), {}};
    }

    const FArdaCudaModuleSource& FArdaCudaModule::GetSource() const noexcept
    {
        return mState->mSource;
    }

    TArdaRHIResult<eastl::string> FArdaCudaModule::GetPtx(uint32_t ComputeCapability) const
    {
        if (!ComputeCapability)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                "CUDA compilation requires a nonzero target architecture.")};
        }
        if (mState->mSource.mLanguage == EArdaCudaSourceLanguage::Ptx)
        {
            return {mState->mSource.mCode, {}};
        }
        std::lock_guard<std::mutex> Lock(mState->mMutex);
        const auto Cached = mState->mPtxByArchitecture.find(ComputeCapability);
        if (Cached != mState->mPtxByArchitecture.end())
        {
            return {Cached->second, {}};
        }
        auto Compiled = mState->mCompiler(mState->mSource, ComputeCapability);
        if (!Compiled)
        {
            return Compiled;
        }
        if (Compiled.mValue.empty() || Compiled.mValue.find('\0') != eastl::string::npos)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                "CUDA compiler returned empty PTX or PTX containing NULs.")};
        }
        mState->mPtxByArchitecture.emplace(ComputeCapability, Compiled.mValue);
        return Compiled;
    }
}
