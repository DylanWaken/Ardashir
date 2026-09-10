#include "ArdaTestComputeOperand.h"

#if defined(ARDA_TEST_PRECOMPILED_CUDA)
namespace arda_cuda_portable {
    void BindAdd(arda::FArdaAddOperand::FRegistry&);
    void BindSurface(arda::FArdaSurfaceOperand::FRegistry&);
}
namespace arda_cuda_tuned { void BindAdd(arda::FArdaAddOperand::FRegistry&); }
#endif
namespace arda
{
    void FArdaAddOperand::BindKernelVariants(FRegistry& Registry) const
    {
#if defined(ARDA_TEST_PRECOMPILED_CUDA)
        arda_cuda_portable::BindAdd(Registry);
        arda_cuda_tuned::BindAdd(Registry);
#endif
    }
    TArdaRHIResult<FArdaCudaKernelSelection> FArdaAddOperand::SelectKernel(const FParameters& P,
        const FArdaCudaSelectionContext&, const FVariants& Candidates) const
    {
        if (!P.mInput.mBuffer || !P.mOutput.mBuffer || !P.mCount || uint64_t(P.mCount) * sizeof(uint32_t) != P.mInput.mRange.Resolve(P.mInput.mBuffer->GetDesc()).mByteSize ||
            uint64_t(P.mCount) * sizeof(uint32_t) != P.mOutput.mRange.Resolve(P.mOutput.mBuffer->GetDesc()).mByteSize)
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Add requires a nonzero count matching both views.")};
        if (P.mInput.mBuffer == P.mOutput.mBuffer)
        {
            const auto A = P.mInput.mRange.Resolve(P.mInput.mBuffer->GetDesc());
            const auto B = P.mOutput.mRange.Resolve(P.mOutput.mBuffer->GetDesc());
            if (A.mByteOffset != B.mByteOffset && A.mByteOffset < B.mByteOffset + B.mByteSize && B.mByteOffset < A.mByteOffset + A.mByteSize)
                return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Add permits identical or disjoint views, but not partial overlap.")};
        }
        const uint32_t Block = P.mCount <= 64 ? 32 : 128;
        for (const bool PreferredOnly : {true, false})
        for (const auto& V : Candidates)
            if (V.mPayload.mBlockSize == Block &&
                (!V.mEntry->GetBuildInfo().mbFastMath || mbPreferFastMath) &&
                (!PreferredOnly || V.mEntry->GetBuildInfo().mbFastMath == mbPreferFastMath))
            {
                FArdaCudaKernelSelection Choice;
                Choice.mVariantId = V.mId;
                Choice.mLaunch.mBlockSize[0] = Block;
                Choice.mLaunch.mGridSize[0] = 1 + (P.mCount - 1) / Block;
                return {Choice, {}};
            }
        return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "No compatible add tile was compiled.")};
    }
    void FArdaSurfaceOperand::BindKernelVariants(FRegistry& Registry) const
    {
#if defined(ARDA_TEST_PRECOMPILED_CUDA)
        arda_cuda_portable::BindSurface(Registry);
#endif
    }
    TArdaRHIResult<FArdaCudaKernelSelection> FArdaSurfaceOperand::SelectKernel(const FParameters& P,
        const FArdaCudaSelectionContext&, const FVariants& Candidates) const
    {
        if (!P.mSurface.mTexture || !P.mWidth || !P.mHeight || Candidates.empty())
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Surface requires nonzero dimensions and a compiled variant.")};
        const auto& Desc = P.mSurface.mTexture->GetDesc();
        const auto Mip = P.mSurface.mRange.mBaseMipLevel;
        if (Mip >= Desc.mMipLevels || Mip >= 32 || Desc.mDimension != EArdaRHITextureDimension::Texture2D ||
            P.mWidth != (Desc.mWidth >> Mip) || P.mHeight != (Desc.mHeight >> Mip))
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Surface dimensions must match the selected 2D mip.")};
        FArdaCudaKernelSelection Choice;
        Choice.mVariantId = Candidates.front().mId;
        Choice.mLaunch.mBlockSize[0] = Choice.mLaunch.mBlockSize[1] = 8;
        Choice.mLaunch.mGridSize[0] = 1 + (P.mWidth - 1) / 8;
        Choice.mLaunch.mGridSize[1] = 1 + (P.mHeight - 1) / 8;
        return {Choice, {}};
    }
}
