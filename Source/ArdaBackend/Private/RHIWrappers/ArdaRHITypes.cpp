#include "RHIWrappers/ArdaRHITypes.h"

#include <EASTL/algorithm.h>
#include <cstring>
#include <functional>

namespace arda::rhi
{
    namespace
    {
        template <typename T>
        void HashCombine(size_t& Seed, const T& Value) noexcept
        {
            Seed ^= std::hash<T>{}(Value) + size_t(0x9e3779b9) + (Seed << 6) + (Seed >> 2);
        }

        void HashString(size_t& Seed, const eastl::string& Value) noexcept
        {
            for (const char Character : Value)
                HashCombine(Seed, static_cast<uint8_t>(Character));
        }

        uint32_t FloatBits(float Value) noexcept
        {
            uint32_t Bits;
            std::memcpy(&Bits, &Value, sizeof(Bits));
            return Bits;
        }
    }

    FArdaRHITextureSubresourceRange FArdaRHITextureSubresourceRange::Resolve(
        const FArdaRHITextureDesc& Desc) const noexcept
    {
        FArdaRHITextureSubresourceRange Result = *this;
        Result.mMipLevelCount = eastl::min(
            Result.mMipLevelCount,
            Desc.mMipLevels - eastl::min(Result.mBaseMipLevel, Desc.mMipLevels));
        Result.mArraySliceCount = eastl::min(
            Result.mArraySliceCount,
            Desc.mArraySize - eastl::min(Result.mBaseArraySlice, Desc.mArraySize));
        return Result;
    }

    FArdaRHIBufferRange FArdaRHIBufferRange::Resolve(
        const FArdaRHIBufferDesc& Desc) const noexcept
    {
        FArdaRHIBufferRange Result = *this;
        Result.mByteOffset = eastl::min(Result.mByteOffset, Desc.mByteSize);
        Result.mByteSize = eastl::min(Result.mByteSize, Desc.mByteSize - Result.mByteOffset);
        return Result;
    }

    bool FArdaRHIBufferRange::IsWholeBuffer(const FArdaRHIBufferDesc& Desc) const noexcept
    {
        const auto Result = Resolve(Desc);
        return Result.mByteOffset == 0 && Result.mByteSize == Desc.mByteSize;
    }

    const FArdaRHIFormatInfo& GetArdaRHIFormatInfo(EArdaRHIFormat Format) noexcept
    {
        static const FArdaRHIFormatInfo Default{};
        static const FArdaRHIFormatInfo Integer{ false, false, true };
        static const FArdaRHIFormatInfo Depth{ true, false, false };
        static const FArdaRHIFormatInfo DepthStencil{ true, true, false };
        switch (Format)
        {
        case EArdaRHIFormat::D16:
        case EArdaRHIFormat::D32: return Depth;
        case EArdaRHIFormat::D24S8:
        case EArdaRHIFormat::D32S8: return DepthStencil;
        case EArdaRHIFormat::R8UInt:
        case EArdaRHIFormat::R8SInt:
        case EArdaRHIFormat::RG8UInt:
        case EArdaRHIFormat::RG8SInt:
        case EArdaRHIFormat::R16UInt:
        case EArdaRHIFormat::R16SInt:
        case EArdaRHIFormat::RGBA8UInt:
        case EArdaRHIFormat::RGBA8SInt:
        case EArdaRHIFormat::RG16UInt:
        case EArdaRHIFormat::RG16SInt:
        case EArdaRHIFormat::R32UInt:
        case EArdaRHIFormat::R32SInt:
        case EArdaRHIFormat::RGBA16UInt:
        case EArdaRHIFormat::RGBA16SInt:
        case EArdaRHIFormat::RG32UInt:
        case EArdaRHIFormat::RG32SInt:
        case EArdaRHIFormat::RGB32UInt:
        case EArdaRHIFormat::RGB32SInt:
        case EArdaRHIFormat::RGBA32UInt:
        case EArdaRHIFormat::RGBA32SInt: return Integer;
        default: return Default;
        }
    }

    bool FArdaRHITextureDesc::operator==(const FArdaRHITextureDesc& O) const noexcept
    {
        return mWidth == O.mWidth && mHeight == O.mHeight && mDepth == O.mDepth &&
            mArraySize == O.mArraySize && mMipLevels == O.mMipLevels &&
            mSampleCount == O.mSampleCount && mFormat == O.mFormat &&
            mDimension == O.mDimension && mUsage == O.mUsage &&
            mInitialState == O.mInitialState && mbKeepInitialState == O.mbKeepInitialState &&
            mbVirtual == O.mbVirtual && mbTiled == O.mbTiled &&
            mClearValue == O.mClearValue && mbUseClearValue == O.mbUseClearValue &&
            mDebugName == O.mDebugName;
    }

    bool FArdaRHIBufferDesc::operator==(const FArdaRHIBufferDesc& O) const noexcept
    {
        return mByteSize == O.mByteSize && mStructureStride == O.mStructureStride &&
            mMaxVersions == O.mMaxVersions && mFormat == O.mFormat && mUsage == O.mUsage &&
            mCpuAccess == O.mCpuAccess && mInitialState == O.mInitialState &&
            mbKeepInitialState == O.mbKeepInitialState && mbVirtual == O.mbVirtual &&
            mDebugName == O.mDebugName;
    }

    bool FArdaRHISamplerDesc::operator==(const FArdaRHISamplerDesc& O) const noexcept
    {
        return mBorderColor == O.mBorderColor && mMaxAnisotropy == O.mMaxAnisotropy &&
            mMipBias == O.mMipBias && mbMinFilter == O.mbMinFilter &&
            mbMagFilter == O.mbMagFilter && mbMipFilter == O.mbMipFilter &&
            mAddressU == O.mAddressU && mAddressV == O.mAddressV && mAddressW == O.mAddressW &&
            mReduction == O.mReduction;
    }

    size_t HashValue(const FArdaRHITextureSubresourceRange& V) noexcept
    {
        size_t H = 0;
        HashCombine(H, V.mBaseMipLevel); HashCombine(H, V.mMipLevelCount);
        HashCombine(H, V.mBaseArraySlice); HashCombine(H, V.mArraySliceCount);
        return H;
    }

    size_t HashValue(const FArdaRHIBufferRange& V) noexcept
    {
        size_t H = 0; HashCombine(H, V.mByteOffset); HashCombine(H, V.mByteSize); return H;
    }

    size_t HashValue(const FArdaRHITextureDesc& V) noexcept
    {
        size_t H = 0;
        HashCombine(H, V.mWidth); HashCombine(H, V.mHeight); HashCombine(H, V.mDepth);
        HashCombine(H, V.mArraySize); HashCombine(H, V.mMipLevels); HashCombine(H, V.mSampleCount);
        HashCombine(H, static_cast<uint8_t>(V.mFormat)); HashCombine(H, static_cast<uint8_t>(V.mDimension));
        HashCombine(H, static_cast<uint16_t>(V.mUsage)); HashCombine(H, static_cast<uint32_t>(V.mInitialState));
        HashCombine(H, V.mbKeepInitialState); HashCombine(H, V.mbVirtual); HashCombine(H, V.mbTiled); HashCombine(H, FloatBits(V.mClearValue.mR));
        HashCombine(H, FloatBits(V.mClearValue.mG)); HashCombine(H, FloatBits(V.mClearValue.mB));
        HashCombine(H, FloatBits(V.mClearValue.mA)); HashCombine(H, V.mbUseClearValue); HashString(H, V.mDebugName);
        return H;
    }

    size_t HashValue(const FArdaRHIBufferDesc& V) noexcept
    {
        size_t H = 0;
        HashCombine(H, V.mByteSize); HashCombine(H, V.mStructureStride); HashCombine(H, V.mMaxVersions);
        HashCombine(H, static_cast<uint8_t>(V.mFormat)); HashCombine(H, static_cast<uint16_t>(V.mUsage));
        HashCombine(H, static_cast<uint8_t>(V.mCpuAccess)); HashCombine(H, static_cast<uint32_t>(V.mInitialState));
        HashCombine(H, V.mbKeepInitialState); HashCombine(H, V.mbVirtual); HashString(H, V.mDebugName); return H;
    }

    size_t HashValue(const FArdaRHISamplerDesc& V) noexcept
    {
        size_t H = 0;
        HashCombine(H, FloatBits(V.mBorderColor.mR)); HashCombine(H, FloatBits(V.mBorderColor.mG));
        HashCombine(H, FloatBits(V.mBorderColor.mB)); HashCombine(H, FloatBits(V.mBorderColor.mA));
        HashCombine(H, FloatBits(V.mMaxAnisotropy)); HashCombine(H, FloatBits(V.mMipBias));
        HashCombine(H, V.mbMinFilter); HashCombine(H, V.mbMagFilter); HashCombine(H, V.mbMipFilter);
        HashCombine(H, static_cast<uint8_t>(V.mAddressU)); HashCombine(H, static_cast<uint8_t>(V.mAddressV));
        HashCombine(H, static_cast<uint8_t>(V.mAddressW)); HashCombine(H, static_cast<uint8_t>(V.mReduction));
        return H;
    }
}
