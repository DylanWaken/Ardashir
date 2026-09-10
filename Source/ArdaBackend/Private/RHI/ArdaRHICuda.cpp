/** @file ArdaRHICuda.cpp
 * Shared descriptor, channel-layout and launch checks for native CUDA providers.
 * These checks constrain bindings and launch metadata, not arbitrary kernel memory accesses.
 */
#include "RHI/ArdaRHICuda.h"
#include "ArdaRHICudaValidation.h"

namespace arda
{
    bool FArdaCudaArchitecture::Supports(uint32_t DeviceCapability) const noexcept
    {
        if (!mComputeCapability) return false;
        if (mbExact || mComputeCapability >= 100) return mComputeCapability == DeviceCapability;
        return mComputeCapability / 10 == DeviceCapability / 10 && mComputeCapability <= DeviceCapability;
    }
    FArdaRHIStatus ValidateArdaCudaBuffer(const FArdaRHIBufferDesc& D)
    {
        if (!D.mByteSize || D.mbVirtual || D.mbTiled || D.mMaxVersions ||
            D.mCpuAccess != EArdaRHICpuAccess::None ||
            HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::AccelStructStorage))
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "CUDA requires a nonempty dedicated device-local buffer; AS storage, versioned, placed and sparse buffers are unsupported.");
        return {};
    }

    FArdaRHIStatus ValidateArdaCudaTexture(const FArdaRHITextureDesc& D)
    {
        if (D.mbVirtual || D.mbTiled || D.mSampleCount != 1 ||
            (D.mDimension != EArdaRHITextureDimension::Texture1D &&
                D.mDimension != EArdaRHITextureDimension::Texture1DArray &&
                D.mDimension != EArdaRHITextureDimension::Texture2D &&
                D.mDimension != EArdaRHITextureDimension::Texture2DArray &&
                D.mDimension != EArdaRHITextureDimension::Texture3D) ||
            HasAnyFlags(D.mUsage, EArdaRHITextureUsage::DepthStencil))
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "CUDA surfaces require dedicated, non-MSAA, non-depth, non-cube textures.");
        if (GetArdaCudaFormatInfo(D.mFormat).mChannels) return {};
        return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
            "CUDA surfaces require uncompressed integer or floating-point R/RG/RGBA formats; color conversion is explicit.");
    }

    FArdaCudaFormatInfo GetArdaCudaFormatInfo(EArdaRHIFormat Format) noexcept
    {
        // This is the sole channel-layout table used by admission and native imports.
        // Normalized/packed/color formats need an explicit conversion operator.
        switch (Format)
        {
        case EArdaRHIFormat::R8UInt: return {EArdaCudaScalarType::UInt, 8, 1};
        case EArdaRHIFormat::R8SInt: return {EArdaCudaScalarType::SInt, 8, 1};
        case EArdaRHIFormat::RG8UInt: return {EArdaCudaScalarType::UInt, 8, 2};
        case EArdaRHIFormat::RG8SInt: return {EArdaCudaScalarType::SInt, 8, 2};
        case EArdaRHIFormat::RGBA8UInt: return {EArdaCudaScalarType::UInt, 8, 4};
        case EArdaRHIFormat::RGBA8SInt: return {EArdaCudaScalarType::SInt, 8, 4};
        case EArdaRHIFormat::R16UInt: return {EArdaCudaScalarType::UInt, 16, 1};
        case EArdaRHIFormat::R16SInt: return {EArdaCudaScalarType::SInt, 16, 1};
        case EArdaRHIFormat::R16Float: return {EArdaCudaScalarType::Float, 16, 1};
        case EArdaRHIFormat::RG16UInt: return {EArdaCudaScalarType::UInt, 16, 2};
        case EArdaRHIFormat::RG16SInt: return {EArdaCudaScalarType::SInt, 16, 2};
        case EArdaRHIFormat::RG16Float: return {EArdaCudaScalarType::Float, 16, 2};
        case EArdaRHIFormat::RGBA16UInt: return {EArdaCudaScalarType::UInt, 16, 4};
        case EArdaRHIFormat::RGBA16SInt: return {EArdaCudaScalarType::SInt, 16, 4};
        case EArdaRHIFormat::RGBA16Float: return {EArdaCudaScalarType::Float, 16, 4};
        case EArdaRHIFormat::R32UInt: return {EArdaCudaScalarType::UInt, 32, 1};
        case EArdaRHIFormat::R32SInt: return {EArdaCudaScalarType::SInt, 32, 1};
        case EArdaRHIFormat::R32Float: return {EArdaCudaScalarType::Float, 32, 1};
        case EArdaRHIFormat::RG32UInt: return {EArdaCudaScalarType::UInt, 32, 2};
        case EArdaRHIFormat::RG32SInt: return {EArdaCudaScalarType::SInt, 32, 2};
        case EArdaRHIFormat::RG32Float: return {EArdaCudaScalarType::Float, 32, 2};
        case EArdaRHIFormat::RGBA32UInt: return {EArdaCudaScalarType::UInt, 32, 4};
        case EArdaRHIFormat::RGBA32SInt: return {EArdaCudaScalarType::SInt, 32, 4};
        case EArdaRHIFormat::RGBA32Float: return {EArdaCudaScalarType::Float, 32, 4};
        default: return {};
        }
    }

    FArdaRHIStatus ValidateArdaCudaKernels(const eastl::vector<FArdaCudaKernel>& Kernels,
        size_t BindingCount, const FArdaCudaCapabilities& C)
    {
        if (!C) return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, C.mUnavailableReason.c_str());
        if (Kernels.size() != 1) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Each CUDA dispatch must contain exactly one kernel.");
        for (const auto& K : Kernels)
        {
            if (!K.mEntry) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA dispatch has no compiled entry.");
            const auto Signature = K.mEntry->GetSignature();
            if (!Signature.mbSupported || !Signature.mType || Signature.mSize != K.mParameters.size() ||
                !Signature.mSize || !Signature.mAlignment || (Signature.mAlignment & (Signature.mAlignment - 1)))
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA parameter signature or size is invalid.");
            bool Supported = false;
            for (const auto& A : K.mEntry->GetBuildInfo().mArchitectures)
                Supported |= A.Supports(C.mComputeCapability);
            if (!Supported) return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "No precompiled native kernel image supports this CUDA architecture.");
            if (auto Status = ValidateArdaCudaLaunch(K, BindingCount, C); !Status)
            {
                return Status;
            }
        }
        return {};
    }
}
