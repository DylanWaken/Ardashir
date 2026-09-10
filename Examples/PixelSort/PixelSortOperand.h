#pragma once
#include "Compute/ArdaComputeOperand.h"

namespace arda
{
    struct FPixelSortColor { uint8_t mR, mG, mB, mA; };
#define ARDA_PIXEL_SORT_FIELDS(VALUE, BUFFER, SURFACE) \
    SURFACE(FPixelSortColor, mInput, EArdaComputeAccess::Read, EArdaRHIFormat::RGBA8UInt) \
    SURFACE(FPixelSortColor, mOutput, EArdaComputeAccess::Write, EArdaRHIFormat::RGBA8UInt) \
    VALUE(uint32_t, mWidth) \
    VALUE(uint32_t, mHeight) \
    VALUE(uint32_t, mChannel) \
    VALUE(uint32_t, mThreshold)
    ARDA_CUDA_PARAMETER_STRUCT(FPixelSortParameters, ARDA_PIXEL_SORT_FIELDS)
    struct FPixelSortVariant { uint32_t mThreads, mTileSize; bool mbVertical; };
    class FPixelSortOperand final : public TArdaComputeOperand<FPixelSortParameters, FPixelSortVariant>
    {
    public:
        using TArdaComputeOperand::TArdaComputeOperand;
        const char* GetName() const noexcept override { return "PixelSort.Radix"; }
        void BindKernelVariants(FRegistry&) const override;
        TArdaRHIResult<FArdaCudaKernelSelection> SelectKernel(const FParameters&,
            const FArdaCudaSelectionContext&, const FVariants&) const override;
    };
}
