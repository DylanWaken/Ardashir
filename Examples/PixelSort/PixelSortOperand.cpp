#include "PixelSortOperand.h"
namespace arda_cuda_pixel_sort { void BindPixelSort(arda::FPixelSortOperand::FRegistry&); }
namespace arda
{
    void FPixelSortOperand::BindKernelVariants(FRegistry& Registry) const
    { arda_cuda_pixel_sort::BindPixelSort(Registry); }

    TArdaRHIResult<FArdaCudaKernelSelection> FPixelSortOperand::SelectKernel(const FParameters& P,
        const FArdaCudaSelectionContext&, const FVariants& Candidates) const
    {
        if (!P.mWidth || !P.mHeight || P.mChannel > 2 || P.mThreshold > 255 ||
            !P.mInput.mTexture || !P.mOutput.mTexture || P.mInput.mTexture == P.mOutput.mTexture)
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "PixelSort needs two distinct textures, valid dimensions and an RGB channel.")};
        for (const auto* Texture : {P.mInput.mTexture.Get(), P.mOutput.mTexture.Get()})
        {
            const auto& D = Texture->GetDesc();
            if (D.mWidth != P.mWidth || D.mHeight != P.mHeight || D.mMipLevels != 1 ||
                D.mDimension != EArdaRHITextureDimension::Texture2D)
                return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "PixelSort dimensions must match a single 2D mip.")};
        }
        const bool Vertical = P.mHeight > P.mWidth;
        for (const auto& V : Candidates)
            if (V.mPayload.mbVertical == Vertical)
            {
                FArdaCudaKernelSelection Choice;
                Choice.mVariantId = V.mId;
                Choice.mLaunch.mBlockSize[0] = V.mPayload.mThreads;
                const auto Length = Vertical ? P.mHeight : P.mWidth;
                Choice.mLaunch.mGridSize[0] = 1 + (Length - 1) / V.mPayload.mTileSize;
                Choice.mLaunch.mGridSize[1] = Vertical ? P.mWidth : P.mHeight;
                return {Choice, {}};
            }
        return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "No native PixelSort variant supports this orientation/device.")};
    }
}
