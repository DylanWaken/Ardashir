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
        if (!P.mInput.mBuffer || !P.mOutput.mBuffer || !P.mCount)
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Add requires a nonzero count matching both views.")};
        const auto InputRange = P.mInput.mRange.Resolve(P.mInput.mBuffer->GetDesc());
        const auto OutputRange = P.mOutput.mRange.Resolve(P.mOutput.mBuffer->GetDesc());
        const auto ByteCount = uint64_t(P.mCount) * sizeof(uint32_t);
        if (ByteCount != InputRange.mByteSize || ByteCount != OutputRange.mByteSize)
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Add count must match both views.")};
        if (P.mInput.mBuffer == P.mOutput.mBuffer)
        {
            if (InputRange.mByteOffset != OutputRange.mByteOffset &&
                InputRange.mByteOffset < OutputRange.mByteOffset + OutputRange.mByteSize &&
                OutputRange.mByteOffset < InputRange.mByteOffset + InputRange.mByteSize)
                return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Add permits identical or disjoint views, but not partial overlap.")};
        }
        const uint32_t Block = P.mCount <= 64 ? 32 : 128;
        const FVariants::value_type* Selected = nullptr;
        for (const auto& V : Candidates)
        {
            const bool FastMath = V.mEntry->GetBuildInfo().mbFastMath;
            if (V.mPayload.mBlockSize != Block || (FastMath && !mbPreferFastMath)) continue;
            if (!Selected) Selected = &V;
            if (FastMath == mbPreferFastMath)
            {
                Selected = &V;
                break;
            }
        }
        if (!Selected)
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "No compatible add tile was compiled.")};
        FArdaCudaKernelSelection Choice;
        Choice.mVariantId = Selected->mId;
        Choice.mLaunch.mBlockSize[0] = Block;
        Choice.mLaunch.mGridSize[0] = 1 + (P.mCount - 1) / Block;
        return {Choice, {}};
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
