#include "RHI/ArdaRHIResources.h"

#include "ArdaHash.h"

#include <cmath>

namespace arda::rhi
{
    namespace
    {
        template <typename T>
        void Combine(size_t& Seed, const T& Value) noexcept
        {
            private_api::HashCombine(Seed, Value);
        }

        void CombineString(size_t& Seed, const eastl::string& Value) noexcept
        {
            private_api::HashString(Seed, Value);
        }

        template <typename T>
        void CombineRef(size_t& Seed, const TArdaRHIRef<T>& Value) noexcept
        {
            Combine(Seed, reinterpret_cast<uintptr_t>(Value.Get()));
        }

        template <typename T>
        void CombineRefs(size_t& Seed, const eastl::vector<TArdaRHIRef<T>>& Values) noexcept
        {
            Combine(Seed, Values.size());
            for (const auto& Value : Values) CombineRef(Seed, Value);
        }

        FArdaRHIStatus Invalid(const char* Message)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
        }

        template <typename PipelineDesc>
        void CombineRasterFixedFunctionState(
            size_t& Hash, const PipelineDesc& Value)
        {
            Combine(Hash, HashValue(Value.mBlendState));
            Combine(Hash, HashValue(Value.mRasterState));
            Combine(Hash, HashValue(Value.mDepthStencilState));
            for (auto Format : Value.mColorFormats)
                Combine(Hash, static_cast<uint8_t>(Format));
            Combine(Hash, static_cast<uint8_t>(Value.mDepthFormat));
            Combine(Hash, Value.mSampleCount);
        }
    }

    FArdaRHIStatus Validate(const FArdaRHITextureDesc& D) noexcept
    {
        if (!D.mWidth || !D.mHeight || !D.mDepth || !D.mArraySize ||
            !D.mMipLevels || !D.mSampleCount)
            return Invalid("Texture dimensions, array size, mip count, and sample count must be non-zero.");
        if (!IsArdaRHIFormatKnown(D.mFormat))
            return Invalid("Texture format must be specified.");
        if (D.mSampleCount > 1 && D.mMipLevels != 1)
            return Invalid("Multisampled textures must have exactly one mip level.");
        if (D.mDimension == EArdaRHITextureDimension::Unknown)
            return Invalid("Texture dimension must be specified.");
        return {};
    }

    FArdaRHIStatus Validate(const FArdaRHIBufferDesc& D) noexcept
    {
        if (!D.mByteSize) return Invalid("Buffer size must be non-zero.");
        if (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::Structured) &&
            !D.mStructureStride)
            return Invalid("Structured buffers require a non-zero structure stride.");
        if (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::Volatile) &&
            !D.mMaxVersions)
            return Invalid("Volatile buffers require a non-zero max version count.");
        return {};
    }

    size_t HashValue(const FArdaRHIViewDesc& V) noexcept
    {
        size_t H = 0;
        Combine(H, static_cast<uint8_t>(V.mFormat));
        Combine(H, static_cast<uint8_t>(V.mDimension));
        Combine(H, HashValue(V.mTextureRange));
        Combine(H, HashValue(V.mBufferRange));
        return H;
    }

    size_t HashValue(const FArdaRHIVertexAttributeDesc& V) noexcept
    {
        size_t H = 0;
        CombineString(H, V.mSemanticName);
        Combine(H, static_cast<uint8_t>(V.mFormat));
        Combine(H, V.mArraySize);
        Combine(H, V.mBufferIndex);
        Combine(H, V.mOffset);
        Combine(H, V.mElementStride);
        Combine(H, V.mbInstanced);
        return H;
    }

    size_t HashValue(const FArdaRHIBindingLayoutDesc& V) noexcept
    {
        size_t H = 0;
        Combine(H, static_cast<uint16_t>(V.mVisibility));
        Combine(H, V.mRegisterSpace);
        Combine(H, V.mbRegisterSpaceIsDescriptorSet);
        Combine(H, V.mItems.size());
        for (const auto& I : V.mItems)
        {
            Combine(H, I.mSlot);
            Combine(H, I.mArraySize);
            Combine(H, static_cast<uint8_t>(I.mType));
        }
        return H;
    }

    size_t HashValue(const FArdaRHIRasterState& V) noexcept
    {
        size_t H = 0;
        Combine(H, static_cast<uint8_t>(V.mFillMode));
        Combine(H, static_cast<uint8_t>(V.mCullMode));
        Combine(H, V.mbFrontCounterClockwise);
        Combine(H, V.mbDepthClip);
        Combine(H, V.mbScissor);
        return H;
    }

    size_t HashValue(const FArdaRHIDepthStencilState& V) noexcept
    {
        size_t H = 0;
        Combine(H, V.mbDepthTest);
        Combine(H, V.mbDepthWrite);
        Combine(H, static_cast<uint8_t>(V.mDepthFunc));
        return H;
    }

    size_t HashValue(const FArdaRHIBlendTargetState& V) noexcept
    {
        size_t H = 0;
        Combine(H, V.mbEnable);
        Combine(H, static_cast<uint8_t>(V.mSourceColor));
        Combine(H, static_cast<uint8_t>(V.mDestinationColor));
        Combine(H, static_cast<uint8_t>(V.mSourceAlpha));
        Combine(H, static_cast<uint8_t>(V.mDestinationAlpha));
        return H;
    }

    size_t HashValue(const FArdaRHIBlendState& V) noexcept
    {
        size_t H = 0;
        Combine(H, V.mbAlphaToCoverage);
        for (const auto& Target : V.mTargets) Combine(H, HashValue(Target));
        return H;
    }

    size_t HashValue(const FArdaRHIInputLayoutDesc& V) noexcept
    {
        size_t H = 0;
        Combine(H, V.mAttributes.size());
        for (const auto& A : V.mAttributes) Combine(H, HashValue(A));
        return H;
    }

    size_t HashValue(const FArdaRHIGraphicsPipelineDesc& V) noexcept
    {
        size_t H = 0;
        Combine(H, static_cast<uint8_t>(V.mTopology));
        Combine(H, V.mPatchControlPoints);
        CombineRef(H, V.mInputLayout);
        CombineRef(H, V.mVertexShader);
        CombineRef(H, V.mHullShader);
        CombineRef(H, V.mDomainShader);
        CombineRef(H, V.mGeometryShader);
        CombineRef(H, V.mPixelShader);
        CombineRefs(H, V.mBindingLayouts);
        CombineRasterFixedFunctionState(H, V);
        return H;
    }

    size_t HashValue(const FArdaRHIComputePipelineDesc& V) noexcept
    {
        size_t H = 0;
        CombineRef(H, V.mComputeShader);
        CombineRefs(H, V.mBindingLayouts);
        return H;
    }

    size_t HashValue(const FArdaRHIMeshletPipelineDesc& V) noexcept
    {
        size_t H = 0;
        Combine(H, static_cast<uint8_t>(V.mTopology));
        CombineRef(H, V.mAmplificationShader);
        CombineRef(H, V.mMeshShader);
        CombineRef(H, V.mPixelShader);
        CombineRefs(H, V.mBindingLayouts);
        CombineRasterFixedFunctionState(H, V);
        return H;
    }

    size_t HashValue(
        const FArdaRHIRayTracingPipelineShaderDesc& V) noexcept
    {
        size_t H = 0;
        CombineString(H, V.mExportName);
        CombineRef(H, V.mShader);
        CombineRef(H, V.mLocalBindingLayout);
        return H;
    }

    size_t HashValue(const FArdaRHIRayTracingHitGroupDesc& V) noexcept
    {
        size_t H = 0;
        CombineString(H, V.mExportName);
        CombineRef(H, V.mClosestHitShader);
        CombineRef(H, V.mAnyHitShader);
        CombineRef(H, V.mIntersectionShader);
        CombineRef(H, V.mLocalBindingLayout);
        Combine(H, V.mbProceduralPrimitive);
        return H;
    }

    size_t HashValue(const FArdaRHIRayTracingPipelineDesc& V) noexcept
    {
        size_t H = 0;
        Combine(H, V.mShaders.size());
        for (const auto& S : V.mShaders) Combine(H, HashValue(S));
        Combine(H, V.mHitGroups.size());
        for (const auto& G : V.mHitGroups) Combine(H, HashValue(G));
        CombineRefs(H, V.mGlobalBindingLayouts);
        Combine(H, V.mMaxPayloadSize);
        Combine(H, V.mMaxAttributeSize);
        Combine(H, V.mMaxRecursionDepth);
        Combine(H, V.mbAllowOpacityMicromaps);
        return H;
    }

    size_t HashValue(const FArdaRHIWorkGraphPipelineDesc& V) noexcept
    {
        size_t H = 0;
        CombineString(H, V.mProgramName);
        CombineString(H, V.mEntryPoint);
        CombineRefs(H, V.mShaders);
        CombineRefs(H, V.mGlobalBindingLayouts);
        Combine(H, V.mMaxInputRecords);
        return H;
    }

    FArdaRHIStatus Validate(const FArdaRHIViewDesc& V) noexcept
    {
        if (V.mTextureRange.mMipLevelCount == 0 ||
            V.mTextureRange.mArraySliceCount == 0 ||
            V.mBufferRange.mByteSize == 0)
            return Invalid("View ranges must not be empty.");
        return {};
    }

    FArdaRHIStatus Validate(const FArdaRHISamplerDesc& V) noexcept
    {
        if (!std::isfinite(V.mMaxAnisotropy) || V.mMaxAnisotropy < 1.f ||
            !std::isfinite(V.mMipBias))
            return Invalid("Sampler anisotropy and mip bias must be finite; anisotropy must be at least one.");
        return {};
    }

    FArdaRHIStatus Validate(const FArdaRHIVertexAttributeDesc& V) noexcept
    {
        if (V.mSemanticName.empty() || !IsArdaRHIFormatKnown(V.mFormat) ||
            V.mArraySize == 0 || V.mElementStride == 0)
            return Invalid("Vertex attributes require a semantic, format, array size, and stride.");
        return {};
    }

    FArdaRHIStatus Validate(const FArdaRHIBindingLayoutDesc& V) noexcept
    {
        if (V.mVisibility == EArdaRHIShaderStage::None || V.mItems.empty())
            return Invalid("Binding layout visibility and items are required.");
        for (size_t I = 0; I < V.mItems.size(); ++I)
        {
            if (V.mItems[I].mArraySize == 0 || V.mItems[I].mArraySize > 65535)
                return Invalid("Binding array size must be between 1 and 65535.");
            for (size_t J = I + 1; J < V.mItems.size(); ++J)
                if (V.mItems[I].mSlot == V.mItems[J].mSlot &&
                    V.mItems[I].mType == V.mItems[J].mType)
                    return Invalid("Binding layout contains a duplicate slot and type.");
        }
        return {};
    }

    FArdaRHIStatus Validate(const FArdaRHIInputLayoutDesc& V)
    {
        if (V.mAttributes.empty()) return Invalid("Input layout requires attributes.");
        for (const auto& A : V.mAttributes)
            if (auto S = Validate(A); !S) return S;
        return {};
    }

    FArdaRHIStatus Validate(const FArdaRHIGraphicsPipelineDesc& V)
    {
        if (!V.mVertexShader) return Invalid("Graphics pipeline requires a vertex shader.");
        if (V.mVertexShader->GetStage() != EArdaRHIShaderStage::Vertex)
            return Invalid("Graphics pipeline vertex shader has the wrong stage.");
        if (V.mHullShader && V.mHullShader->GetStage() != EArdaRHIShaderStage::Hull)
            return Invalid("Graphics pipeline hull shader has the wrong stage.");
        if (V.mDomainShader && V.mDomainShader->GetStage() != EArdaRHIShaderStage::Domain)
            return Invalid("Graphics pipeline domain shader has the wrong stage.");
        if (V.mGeometryShader && V.mGeometryShader->GetStage() != EArdaRHIShaderStage::Geometry)
            return Invalid("Graphics pipeline geometry shader has the wrong stage.");
        if (V.mPixelShader && V.mPixelShader->GetStage() != EArdaRHIShaderStage::Pixel)
            return Invalid("Graphics pipeline pixel shader has the wrong stage.");
        if (V.mSampleCount == 0 || V.mColorFormats.size() > ArdaRHIMaxRenderTargets)
            return Invalid("Graphics pipeline sample count and attachment formats are invalid.");
        if (V.mTopology == EArdaRHIPrimitiveTopology::PatchList &&
            V.mPatchControlPoints == 0)
            return Invalid("Patch-list pipelines require control points.");
        return {};
    }

    FArdaRHIStatus Validate(const FArdaRHIComputePipelineDesc& V)
    {
        if (!V.mComputeShader) return Invalid("Compute pipeline requires a compute shader.");
        return V.mComputeShader->GetStage() == EArdaRHIShaderStage::Compute
            ? FArdaRHIStatus{}
            : Invalid("Compute pipeline shader has the wrong stage.");
    }

    FArdaRHIStatus Validate(const FArdaRHIMeshletPipelineDesc& V)
    {
        if (!V.mMeshShader) return Invalid("Meshlet pipeline requires a mesh shader.");
        if (V.mMeshShader->GetStage() != EArdaRHIShaderStage::Mesh)
            return Invalid("Meshlet pipeline mesh shader has the wrong stage.");
        if (V.mAmplificationShader &&
            V.mAmplificationShader->GetStage() != EArdaRHIShaderStage::Amplification)
            return Invalid("Meshlet pipeline amplification shader has the wrong stage.");
        if (V.mPixelShader && V.mPixelShader->GetStage() != EArdaRHIShaderStage::Pixel)
            return Invalid("Meshlet pipeline pixel shader has the wrong stage.");
        if (V.mSampleCount == 0 || V.mColorFormats.size() > ArdaRHIMaxRenderTargets)
            return Invalid("Meshlet pipeline sample count and attachment formats are invalid.");
        return {};
    }

    FArdaRHIStatus Validate(const FArdaRHIRayTracingPipelineDesc& V)
    {
        if (V.mShaders.empty() || V.mMaxRecursionDepth == 0)
            return Invalid("Ray-tracing pipelines require shaders and non-zero recursion depth.");
        for (const auto& S : V.mShaders)
        {
            if (S.mExportName.empty() || !S.mShader ||
                !HasAnyFlags(
                    EArdaRHIShaderStage::AllRayTracing,
                    S.mShader->GetStage()))
                return Invalid("Ray-tracing pipeline shader exports require a name and ray-tracing shader.");
        }
        for (const auto& H : V.mHitGroups)
        {
            if (H.mExportName.empty() ||
                (!H.mClosestHitShader && !H.mAnyHitShader &&
                 !H.mIntersectionShader))
                return Invalid("Ray-tracing hit groups require a name and at least one shader.");
            if (H.mbProceduralPrimitive && !H.mIntersectionShader)
                return Invalid("Procedural ray-tracing hit groups require an intersection shader.");
        }
        return {};
    }
}
