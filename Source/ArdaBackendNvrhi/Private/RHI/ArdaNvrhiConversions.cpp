#include "RHI/ArdaNvrhiConversions.h"

namespace arda::rhi::private_impl
{
    nvrhi::Format ToNvrhi(EArdaRHIFormat V) noexcept
    {
#define ARDA_FORMAT(Name, Native) case EArdaRHIFormat::Name: return nvrhi::Format::Native
        switch (V)
        {
        ARDA_FORMAT(Unknown, UNKNOWN);
        ARDA_FORMAT(R8UInt, R8_UINT); ARDA_FORMAT(R8SInt, R8_SINT); ARDA_FORMAT(R8UNorm, R8_UNORM); ARDA_FORMAT(R8SNorm, R8_SNORM);
        ARDA_FORMAT(RG8UInt, RG8_UINT); ARDA_FORMAT(RG8SInt, RG8_SINT); ARDA_FORMAT(RG8UNorm, RG8_UNORM); ARDA_FORMAT(RG8SNorm, RG8_SNORM);
        ARDA_FORMAT(R16UInt, R16_UINT); ARDA_FORMAT(R16SInt, R16_SINT); ARDA_FORMAT(R16UNorm, R16_UNORM); ARDA_FORMAT(R16SNorm, R16_SNORM); ARDA_FORMAT(R16Float, R16_FLOAT);
        ARDA_FORMAT(RGBA8UInt, RGBA8_UINT); ARDA_FORMAT(RGBA8SInt, RGBA8_SINT); ARDA_FORMAT(RGBA8UNorm, RGBA8_UNORM); ARDA_FORMAT(RGBA8SNorm, RGBA8_SNORM);
        ARDA_FORMAT(BGRA8UNorm, BGRA8_UNORM); ARDA_FORMAT(SRGBA8UNorm, SRGBA8_UNORM); ARDA_FORMAT(SBGRA8UNorm, SBGRA8_UNORM);
        ARDA_FORMAT(R10G10B10A2UNorm, R10G10B10A2_UNORM); ARDA_FORMAT(R11G11B10Float, R11G11B10_FLOAT);
        ARDA_FORMAT(RG16UInt, RG16_UINT); ARDA_FORMAT(RG16SInt, RG16_SINT); ARDA_FORMAT(RG16UNorm, RG16_UNORM); ARDA_FORMAT(RG16SNorm, RG16_SNORM); ARDA_FORMAT(RG16Float, RG16_FLOAT);
        ARDA_FORMAT(R32UInt, R32_UINT); ARDA_FORMAT(R32SInt, R32_SINT); ARDA_FORMAT(R32Float, R32_FLOAT);
        ARDA_FORMAT(RGBA16UInt, RGBA16_UINT); ARDA_FORMAT(RGBA16SInt, RGBA16_SINT); ARDA_FORMAT(RGBA16Float, RGBA16_FLOAT); ARDA_FORMAT(RGBA16UNorm, RGBA16_UNORM); ARDA_FORMAT(RGBA16SNorm, RGBA16_SNORM);
        ARDA_FORMAT(RG32UInt, RG32_UINT); ARDA_FORMAT(RG32SInt, RG32_SINT); ARDA_FORMAT(RG32Float, RG32_FLOAT);
        ARDA_FORMAT(RGB32UInt, RGB32_UINT); ARDA_FORMAT(RGB32SInt, RGB32_SINT); ARDA_FORMAT(RGB32Float, RGB32_FLOAT);
        ARDA_FORMAT(RGBA32UInt, RGBA32_UINT); ARDA_FORMAT(RGBA32SInt, RGBA32_SINT); ARDA_FORMAT(RGBA32Float, RGBA32_FLOAT);
        ARDA_FORMAT(D16, D16); ARDA_FORMAT(D24S8, D24S8); ARDA_FORMAT(D32, D32); ARDA_FORMAT(D32S8, D32S8);
        ARDA_FORMAT(BC1UNorm, BC1_UNORM); ARDA_FORMAT(BC1UNormSRGB, BC1_UNORM_SRGB); ARDA_FORMAT(BC2UNorm, BC2_UNORM); ARDA_FORMAT(BC2UNormSRGB, BC2_UNORM_SRGB);
        ARDA_FORMAT(BC3UNorm, BC3_UNORM); ARDA_FORMAT(BC3UNormSRGB, BC3_UNORM_SRGB); ARDA_FORMAT(BC4UNorm, BC4_UNORM); ARDA_FORMAT(BC4SNorm, BC4_SNORM);
        ARDA_FORMAT(BC5UNorm, BC5_UNORM); ARDA_FORMAT(BC5SNorm, BC5_SNORM); ARDA_FORMAT(BC6HUFloat, BC6H_UFLOAT); ARDA_FORMAT(BC6HSFloat, BC6H_SFLOAT);
        ARDA_FORMAT(BC7UNorm, BC7_UNORM); ARDA_FORMAT(BC7UNormSRGB, BC7_UNORM_SRGB);
        }
#undef ARDA_FORMAT
        return nvrhi::Format::UNKNOWN;
    }

    nvrhi::TextureDimension ToNvrhi(EArdaRHITextureDimension V) noexcept
    {
        return static_cast<nvrhi::TextureDimension>(V);
    }

    nvrhi::ShaderType ToNvrhi(EArdaRHIShaderStage V) noexcept
    {
        return static_cast<nvrhi::ShaderType>(static_cast<uint16_t>(V));
    }

    nvrhi::CommandQueue ToNvrhi(EArdaRHIQueueType V) noexcept
    {
        return static_cast<nvrhi::CommandQueue>(V);
    }

    nvrhi::ResourceStates ToNvrhi(EArdaRHIResourceState V) noexcept
    {
        nvrhi::ResourceStates R = nvrhi::ResourceStates::Unknown;
#define ARDA_STATE(Arda, Native) if (HasAnyFlags(V, EArdaRHIResourceState::Arda)) R = R | nvrhi::ResourceStates::Native
        ARDA_STATE(Common, Common); ARDA_STATE(ConstantBuffer, ConstantBuffer); ARDA_STATE(VertexBuffer, VertexBuffer);
        ARDA_STATE(IndexBuffer, IndexBuffer); ARDA_STATE(IndirectArgument, IndirectArgument);
        ARDA_STATE(PixelShaderResource, PixelShaderResource); ARDA_STATE(NonPixelShaderResource, NonPixelShaderResource);
        ARDA_STATE(UnorderedAccess, UnorderedAccess); ARDA_STATE(RenderTarget, RenderTarget);
        ARDA_STATE(DepthWrite, DepthWrite); ARDA_STATE(DepthRead, DepthRead); ARDA_STATE(CopyDest, CopyDest);
        ARDA_STATE(CopySource, CopySource); ARDA_STATE(ResolveDest, ResolveDest); ARDA_STATE(ResolveSource, ResolveSource);
        ARDA_STATE(Present, Present);
        ARDA_STATE(AccelStructRead, AccelStructRead); ARDA_STATE(AccelStructWrite, AccelStructWrite);
        ARDA_STATE(AccelStructBuildInput, AccelStructBuildInput); ARDA_STATE(AccelStructBuildBlas, AccelStructBuildBlas);
        ARDA_STATE(OpacityMicromapWrite, OpacityMicromapWrite); ARDA_STATE(OpacityMicromapBuildInput, OpacityMicromapBuildInput);
#undef ARDA_STATE
        return R;
    }

    nvrhi::TextureSubresourceSet ToNvrhi(const FArdaRHITextureSubresourceRange& V) noexcept
    {
        return { V.mBaseMipLevel, V.mMipLevelCount, V.mBaseArraySlice, V.mArraySliceCount };
    }

    nvrhi::BufferRange ToNvrhi(const FArdaRHIBufferRange& V) noexcept
    {
        return { V.mByteOffset, V.mByteSize };
    }

    nvrhi::TextureDesc ToNvrhi(const FArdaRHITextureDesc& V)
    {
        nvrhi::TextureDesc R;
        R.width = V.mWidth; R.height = V.mHeight; R.depth = V.mDepth; R.arraySize = V.mArraySize;
        R.mipLevels = V.mMipLevels; R.sampleCount = V.mSampleCount; R.format = ToNvrhi(V.mFormat);
        R.dimension = ToNvrhi(V.mDimension); R.debugName = V.mDebugName.c_str();
        R.isShaderResource = HasAnyFlags(V.mUsage, EArdaRHITextureUsage::ShaderResource);
        R.isRenderTarget = HasAnyFlags(V.mUsage, EArdaRHITextureUsage::RenderTarget) || HasAnyFlags(V.mUsage, EArdaRHITextureUsage::DepthStencil);
        R.isUAV = HasAnyFlags(V.mUsage, EArdaRHITextureUsage::UnorderedAccess);
        R.isTypeless = HasAnyFlags(V.mUsage, EArdaRHITextureUsage::Typeless);
        R.initialState = ToNvrhi(V.mInitialState); R.keepInitialState = V.mbKeepInitialState; R.isVirtual = V.mbVirtual; R.isTiled = V.mbTiled;
        R.clearValue = { V.mClearValue.mR, V.mClearValue.mG, V.mClearValue.mB, V.mClearValue.mA };
        R.useClearValue = V.mbUseClearValue;
        return R;
    }

    nvrhi::BufferDesc ToNvrhi(const FArdaRHIBufferDesc& V)
    {
        nvrhi::BufferDesc R;
        R.byteSize = V.mByteSize; R.structStride = V.mStructureStride; R.maxVersions = V.mMaxVersions;
        R.debugName = V.mDebugName.c_str(); R.format = ToNvrhi(V.mFormat);
        R.canHaveUAVs = HasAnyFlags(V.mUsage, EArdaRHIBufferUsage::UnorderedAccess);
        R.canHaveTypedViews = V.mFormat != EArdaRHIFormat::Unknown;
        R.canHaveRawViews = HasAnyFlags(V.mUsage, EArdaRHIBufferUsage::Raw);
        R.isVertexBuffer = HasAnyFlags(V.mUsage, EArdaRHIBufferUsage::Vertex);
        R.isIndexBuffer = HasAnyFlags(V.mUsage, EArdaRHIBufferUsage::Index);
        R.isConstantBuffer = HasAnyFlags(V.mUsage, EArdaRHIBufferUsage::Constant);
        R.isDrawIndirectArgs = HasAnyFlags(V.mUsage, EArdaRHIBufferUsage::Indirect);
        R.isAccelStructBuildInput = HasAnyFlags(V.mUsage, EArdaRHIBufferUsage::AccelStructBuildInput);
        R.isAccelStructStorage = HasAnyFlags(V.mUsage, EArdaRHIBufferUsage::AccelStructStorage);
        R.isShaderBindingTable = HasAnyFlags(V.mUsage, EArdaRHIBufferUsage::ShaderBindingTable);
        R.isVolatile = HasAnyFlags(V.mUsage, EArdaRHIBufferUsage::Volatile);
        R.initialState = ToNvrhi(V.mInitialState); R.keepInitialState = V.mbKeepInitialState; R.isVirtual = V.mbVirtual;
        R.cpuAccess = static_cast<nvrhi::CpuAccessMode>(V.mCpuAccess);
        return R;
    }

    nvrhi::SamplerDesc ToNvrhi(const FArdaRHISamplerDesc& V)
    {
        nvrhi::SamplerDesc R;
        R.borderColor = { V.mBorderColor.mR, V.mBorderColor.mG, V.mBorderColor.mB, V.mBorderColor.mA };
        R.maxAnisotropy = V.mMaxAnisotropy; R.mipBias = V.mMipBias;
        R.minFilter = V.mbMinFilter; R.magFilter = V.mbMagFilter; R.mipFilter = V.mbMipFilter;
        R.addressU = static_cast<nvrhi::SamplerAddressMode>(V.mAddressU);
        R.addressV = static_cast<nvrhi::SamplerAddressMode>(V.mAddressV);
        R.addressW = static_cast<nvrhi::SamplerAddressMode>(V.mAddressW);
        R.reductionType = static_cast<nvrhi::SamplerReductionType>(V.mReduction);
        return R;
    }

}
