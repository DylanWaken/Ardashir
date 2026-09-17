#include "RHI/ArdaRHIResources.h"
#include "RHI/ArdaRHICapabilities.h"

#include "ArdaHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{
	namespace
	{
		template <typename T>
		void Combine(size_t& Seed, const T& Value) noexcept
		{
			ArdaHashCombine(Seed, Value);
		}

		void CombineString(size_t& Seed, const eastl::string& Value) noexcept
		{
			ArdaHashString(Seed, Value);
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
			for (const auto& Value : Values)
			{
				CombineRef(Seed, Value);
			}
		}

		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		template <typename Descriptor, typename Usage>
		bool AllowsInitialState(const Descriptor& Desc, EArdaRHIResourceState State, Usage RequiredUsage)
		{
			return !HasAnyFlags(Desc.mInitialState, State) || HasAnyFlags(Desc.mUsage, RequiredUsage);
		}

		template <typename PipelineDesc>
		void CombineRasterFixedFunctionState(size_t& Hash, const PipelineDesc& Value)
		{
			Combine(Hash, HashValue(Value.mBlendState));
			Combine(Hash, HashValue(Value.mRasterState));
			Combine(Hash, HashValue(Value.mDepthStencilState));
			for (auto Format : Value.mColorFormats)
			{
				Combine(Hash, static_cast<uint8_t>(Format));
			}
			Combine(Hash, static_cast<uint8_t>(Value.mDepthFormat));
			Combine(Hash, Value.mSampleCount);
		}
	}

	FArdaRHIStatus Validate(const FArdaRHITextureDesc& D) noexcept
	{
		if (!D.mWidth || !D.mHeight || !D.mDepth || !D.mArraySize || !D.mMipLevels || !D.mSampleCount)
		{
			return Invalid("Texture dimensions, array size, mip count, and sample count must be non-zero.");
		}
		if (!IsArdaRHIFormatKnown(D.mFormat))
		{
			return Invalid("Texture format must be specified.");
		}
		if (D.mDimension <= EArdaRHITextureDimension::Unknown || D.mDimension > EArdaRHITextureDimension::Texture3D)
		{
			return Invalid("Texture dimension is invalid.");
		}
		constexpr auto KnownUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess |
		    EArdaRHITextureUsage::RenderTarget | EArdaRHITextureUsage::DepthStencil | EArdaRHITextureUsage::Typeless;
		if ((static_cast<uint16_t>(D.mUsage) & ~static_cast<uint16_t>(KnownUsage)) != 0)
		{
			return Invalid("Texture usage contains unknown flags.");
		}
		const bool b1D = D.mDimension == EArdaRHITextureDimension::Texture1D ||
		    D.mDimension == EArdaRHITextureDimension::Texture1DArray;
		const bool b3D = D.mDimension == EArdaRHITextureDimension::Texture3D;
		const bool bCube = D.mDimension == EArdaRHITextureDimension::TextureCube ||
		    D.mDimension == EArdaRHITextureDimension::TextureCubeArray;
		const bool bMultisampleDimension = D.mDimension == EArdaRHITextureDimension::Texture2DMS ||
		    D.mDimension == EArdaRHITextureDimension::Texture2DMSArray;
		const bool bArray = D.mDimension == EArdaRHITextureDimension::Texture1DArray ||
		    D.mDimension == EArdaRHITextureDimension::Texture2DArray ||
		    D.mDimension == EArdaRHITextureDimension::Texture2DMSArray || bCube;
		if ((b1D && D.mHeight != 1) || (!b3D && D.mDepth != 1) || (!bArray && D.mArraySize != 1))
		{
			return Invalid("Texture extents and array size do not match its dimension.");
		}
		if (bCube &&
		    (D.mWidth != D.mHeight || D.mArraySize % 6 != 0 ||
		        (D.mDimension == EArdaRHITextureDimension::TextureCube && D.mArraySize != 6)))
		{
			return Invalid("Cube textures require square faces and six layers per cube.");
		}
		if ((D.mSampleCount & (D.mSampleCount - 1)) != 0 || D.mSampleCount > 64 ||
		    (bMultisampleDimension && D.mSampleCount == 1))
		{
			return Invalid("Texture sample count must be a supported power of two and match its dimension.");
		}
		if (D.mSampleCount > 1 && (D.mMipLevels != 1 || b1D || b3D || bCube))
		{
			return Invalid("Multisampled textures require a two-dimensional shape and exactly one mip level.");
		}
		uint32_t MaxMipLevels = 1;
		for (uint32_t Extent = eastl::max(D.mWidth, eastl::max(D.mHeight, D.mDepth)); Extent > 1; Extent >>= 1)
		{
			++MaxMipLevels;
		}
		if (D.mMipLevels > MaxMipLevels)
		{
			return Invalid("Texture mip count exceeds the chain permitted by its extents.");
		}
		const auto& Format = GetArdaRHIFormatInfo(D.mFormat);
		const bool bDepthAttachment = HasAnyFlags(D.mUsage, EArdaRHITextureUsage::DepthStencil);
		const bool bColorAttachment = HasAnyFlags(D.mUsage, EArdaRHITextureUsage::RenderTarget);
		const bool bStorage = HasAnyFlags(D.mUsage, EArdaRHITextureUsage::UnorderedAccess);
		if ((bDepthAttachment && !Format.mbDepth) || (Format.mbDepth && (bColorAttachment || bStorage)) ||
		    (Format.mBlockWidth > 1 && (bDepthAttachment || bColorAttachment || bStorage || D.mSampleCount > 1)))
		{
			return Invalid("Texture format is incompatible with its attachment, storage, or multisample usage.");
		}
		constexpr auto TextureStates = EArdaRHIResourceState::Common | EArdaRHIResourceState::ShaderResource |
		    EArdaRHIResourceState::UnorderedAccess | EArdaRHIResourceState::RenderTarget |
		    EArdaRHIResourceState::DepthRead | EArdaRHIResourceState::DepthWrite | EArdaRHIResourceState::CopySource |
		    EArdaRHIResourceState::CopyDest | EArdaRHIResourceState::ResolveSource |
		    EArdaRHIResourceState::ResolveDest | EArdaRHIResourceState::Present | EArdaRHIResourceState::Discard |
		    EArdaRHIResourceState::ShadingRateSource;
		if ((static_cast<uint32_t>(D.mInitialState) & ~static_cast<uint32_t>(TextureStates)) != 0)
		{
			return Invalid("Texture initial state contains an unknown or buffer-only state.");
		}
		if (!AllowsInitialState(D, EArdaRHIResourceState::ShaderResource, EArdaRHITextureUsage::ShaderResource) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::UnorderedAccess, EArdaRHITextureUsage::UnorderedAccess) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::RenderTarget, EArdaRHITextureUsage::RenderTarget) ||
		    !AllowsInitialState(D,
		        EArdaRHIResourceState::DepthRead | EArdaRHIResourceState::DepthWrite,
		        EArdaRHITextureUsage::DepthStencil))
		{
			return Invalid("Texture initial state requires its matching declared usage.");
		}
		if (D.mbVirtual && D.mbTiled)
		{
			return Invalid("Texture storage cannot be both virtual and tiled.");
		}
		return {};
	}

	FArdaRHIStatus Validate(const FArdaRHIBufferDesc& D) noexcept
	{
		if (!D.mByteSize)
		{
			return Invalid("Buffer size must be non-zero.");
		}
		constexpr auto KnownUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess |
		    EArdaRHIBufferUsage::Vertex | EArdaRHIBufferUsage::Index | EArdaRHIBufferUsage::Constant |
		    EArdaRHIBufferUsage::Indirect | EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::Structured |
		    EArdaRHIBufferUsage::Volatile | EArdaRHIBufferUsage::AccelStructBuildInput |
		    EArdaRHIBufferUsage::AccelStructStorage | EArdaRHIBufferUsage::ShaderBindingTable |
		    EArdaRHIBufferUsage::OpacityMicromapBuildInput;
		if ((static_cast<uint16_t>(D.mUsage) & ~static_cast<uint16_t>(KnownUsage)) != 0 ||
		    D.mCpuAccess > EArdaRHICpuAccess::Write || D.mFormat >= EArdaRHIFormat::Count)
		{
			return Invalid("Buffer usage, CPU access, or format is invalid.");
		}
		if (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::Structured) &&
		    (!D.mStructureStride || D.mStructureStride % 4 != 0 || D.mStructureStride > D.mByteSize))
		{
			return Invalid("Structured buffers require a DWORD-aligned stride and storage for at least one element.");
		}
		if (D.mFormat != EArdaRHIFormat::Unknown &&
		    (GetArdaRHIFormatInfo(D.mFormat).mbDepth || !GetArdaRHIFormatElementSize(D.mFormat)))
		{
			return Invalid("Typed buffer formats must be uncompressed color formats.");
		}
		if (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::Volatile) && !D.mMaxVersions)
		{
			return Invalid("Volatile buffers require a non-zero max version count.");
		}
		constexpr auto BufferStates = EArdaRHIResourceState::Common | EArdaRHIResourceState::ConstantBuffer |
		    EArdaRHIResourceState::VertexBuffer | EArdaRHIResourceState::IndexBuffer |
		    EArdaRHIResourceState::IndirectArgument | EArdaRHIResourceState::ShaderResource |
		    EArdaRHIResourceState::UnorderedAccess | EArdaRHIResourceState::CopySource |
		    EArdaRHIResourceState::CopyDest | EArdaRHIResourceState::AccelStructRead |
		    EArdaRHIResourceState::AccelStructWrite | EArdaRHIResourceState::AccelStructBuildInput |
		    EArdaRHIResourceState::AccelStructBuildBlas | EArdaRHIResourceState::CpuRead |
		    EArdaRHIResourceState::OpacityMicromapWrite | EArdaRHIResourceState::OpacityMicromapBuildInput |
		    EArdaRHIResourceState::Discard;
		if ((static_cast<uint32_t>(D.mInitialState) & ~static_cast<uint32_t>(BufferStates)) != 0)
		{
			return Invalid("Buffer initial state contains an unknown or texture-only state.");
		}
		if (!AllowsInitialState(D, EArdaRHIResourceState::ShaderResource, EArdaRHIBufferUsage::ShaderResource) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::UnorderedAccess, EArdaRHIBufferUsage::UnorderedAccess) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::VertexBuffer, EArdaRHIBufferUsage::Vertex) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::IndexBuffer, EArdaRHIBufferUsage::Index) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::ConstantBuffer, EArdaRHIBufferUsage::Constant) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::IndirectArgument, EArdaRHIBufferUsage::Indirect))
		{
			return Invalid("Buffer initial state requires its matching declared usage.");
		}
		if ((D.mbVirtual && D.mbTiled) || (D.mCpuAccess != EArdaRHICpuAccess::None && (D.mbTiled || D.mbCudaInterop)))
		{
			return Invalid("Buffer storage flags conflict with virtual, tiled, or CPU-visible storage.");
		}
		if (D.mCpuAccess == EArdaRHICpuAccess::Write &&
		    (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::AccelStructStorage) ||
		        HasAnyFlags(D.mInitialState,
		            EArdaRHIResourceState::UnorderedAccess | EArdaRHIResourceState::CopyDest |
		                EArdaRHIResourceState::AccelStructWrite | EArdaRHIResourceState::OpacityMicromapWrite)))
		{
			return Invalid("CPU-upload buffers cannot be GPU-write destinations.");
		}
		constexpr auto ReadbackStates =
		    EArdaRHIResourceState::Common | EArdaRHIResourceState::CopyDest | EArdaRHIResourceState::CpuRead;
		constexpr auto ReadbackUsage = EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::Structured;
		if (D.mCpuAccess == EArdaRHICpuAccess::Read &&
		    ((static_cast<uint16_t>(D.mUsage) & ~static_cast<uint16_t>(ReadbackUsage)) != 0 ||
		        (static_cast<uint32_t>(D.mInitialState) & ~static_cast<uint32_t>(ReadbackStates)) != 0))
		{
			return Invalid("CPU-readback buffers only support copy destinations and CPU reads.");
		}
		return {};
	}

	FArdaRHIStatus ValidateResourceCapabilities(const FArdaRHITextureDesc& D,
	    const FArdaRHICapabilities& C,
	    const FArdaRHIFormatSupport& F) noexcept
	{
		if (const auto Status = Validate(D); !Status)
		{
			return Status;
		}
		const auto Unsupported = [](const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Message);
		};
		const bool b1D = D.mDimension == EArdaRHITextureDimension::Texture1D ||
		    D.mDimension == EArdaRHITextureDimension::Texture1DArray;
		const bool b3D = D.mDimension == EArdaRHITextureDimension::Texture3D;
		const bool bCube = D.mDimension == EArdaRHITextureDimension::TextureCube ||
		    D.mDimension == EArdaRHITextureDimension::TextureCubeArray;
		const uint32_t MaxExtent = b1D ? C.mLimits.mMaxTexture1D
		    : b3D                      ? C.mLimits.mMaxTexture3D
		    : bCube                    ? C.mLimits.mMaxTextureCube
		                               : C.mLimits.mMaxTexture2D;
		if ((MaxExtent && eastl::max(D.mWidth, eastl::max(D.mHeight, D.mDepth)) > MaxExtent) ||
		    (C.mLimits.mMaxTextureArrayLayers && D.mArraySize > C.mLimits.mMaxTextureArrayLayers))
		{
			return Unsupported("Texture extents or array layers exceed the device limits.");
		}
		if ((D.mbVirtual && !C.mbVirtualResources) ||
		    (D.mbTiled && (b3D ? !C.mResidency.mbReservedTexture3D : b1D || !C.mResidency.mbReservedTexture2D)))
		{
			return Unsupported("Requested virtual or tiled texture storage is unsupported by the device.");
		}
		if (!C.mbFormatSupportReported)
		{
			return {};
		}
		const bool bDimension = b1D ? F.mbTexture1D : b3D ? F.mbTexture3D : bCube ? F.mbTextureCube : F.mbTexture2D;
		if (!F.mNativeFormat || !bDimension || !(F.mSampleCounts & D.mSampleCount))
		{
			return Unsupported("Texture format, dimension, or sample count is unsupported by the device.");
		}
		if ((HasAnyFlags(D.mUsage, EArdaRHITextureUsage::ShaderResource) && !F.mbShaderResource) ||
		    (HasAnyFlags(D.mUsage, EArdaRHITextureUsage::UnorderedAccess) && !F.mbStorage) ||
		    (HasAnyFlags(D.mUsage, EArdaRHITextureUsage::RenderTarget) && !F.mbColorAttachment) ||
		    (HasAnyFlags(D.mUsage, EArdaRHITextureUsage::DepthStencil) && !F.mbDepthStencilAttachment))
		{
			return Unsupported("Texture format does not support a requested usage on this device.");
		}
		return {};
	}

	FArdaRHIStatus ValidateResourceCapabilities(const FArdaRHIBufferDesc& D,
	    const FArdaRHICapabilities& C,
	    const FArdaRHIFormatSupport& F) noexcept
	{
		if (const auto Status = Validate(D); !Status)
		{
			return Status;
		}
		if ((C.mLimits.mMaxBufferSize && D.mByteSize > C.mLimits.mMaxBufferSize) ||
		    (D.mbVirtual && !C.mbVirtualResources) || (D.mbTiled && !C.mResidency.mbReservedBuffers))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Buffer capacity or requested storage mode is unsupported by the device.");
		}
		// Raw and structured views have no native typed format, even when the descriptor supplies a typed fallback.
		if (C.mbFormatSupportReported && D.mFormat != EArdaRHIFormat::Unknown &&
		    !HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::Structured) &&
		    (!F.mNativeFormat ||
		        (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::ShaderResource) && !F.mbBufferShaderResource) ||
		        (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::UnorderedAccess) && !F.mbBufferStorage) ||
		        (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::Vertex) && !F.mbVertexBuffer)))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Typed buffer format does not support a requested usage on this device.");
		}
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
		for (const auto& Target : V.mTargets)
		{
			Combine(H, HashValue(Target));
		}
		return H;
	}

	size_t HashValue(const FArdaRHIInputLayoutDesc& V) noexcept
	{
		size_t H = 0;
		Combine(H, V.mAttributes.size());
		for (const auto& A : V.mAttributes)
		{
			Combine(H, HashValue(A));
		}
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

	size_t HashValue(const FArdaRHIRayTracingPipelineShaderDesc& V) noexcept
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
		for (const auto& S : V.mShaders)
		{
			Combine(H, HashValue(S));
		}
		Combine(H, V.mHitGroups.size());
		for (const auto& G : V.mHitGroups)
		{
			Combine(H, HashValue(G));
		}
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
		if (V.mTextureRange.mMipLevelCount == 0 || V.mTextureRange.mArraySliceCount == 0 ||
		    V.mBufferRange.mByteSize == 0)
		{
			return Invalid("View ranges must not be empty.");
		}
		return {};
	}

	FArdaRHIStatus Validate(const FArdaRHISamplerDesc& V) noexcept
	{
		if (!std::isfinite(V.mMaxAnisotropy) || V.mMaxAnisotropy < 1.f || !std::isfinite(V.mMipBias))
		{
			return Invalid("Sampler anisotropy and mip bias must be finite; anisotropy must be at least one.");
		}
		return {};
	}

	FArdaRHIStatus Validate(const FArdaRHIVertexAttributeDesc& V) noexcept
	{
		if (V.mSemanticName.empty() || !IsArdaRHIFormatKnown(V.mFormat) || V.mArraySize == 0 || V.mElementStride == 0)
		{
			return Invalid("Vertex attributes require a semantic, format, array size, and stride.");
		}
		const uint32_t ElementSize = GetArdaRHIFormatElementSize(V.mFormat);
		const uint32_t Alignment = GetArdaRHIVertexFormatAlignment(V.mFormat);
		if (!Alignment)
		{
			return Invalid("Vertex attributes require an uncompressed color format.");
		}
		if (V.mOffset % Alignment || V.mElementStride % Alignment)
		{
			return Invalid("Vertex attribute offset and stride must align to the format component size.");
		}
		const uint64_t ByteSize = uint64_t(ElementSize) * V.mArraySize;
		if (V.mOffset > V.mElementStride || ByteSize > V.mElementStride - V.mOffset)
		{
			return Invalid("Vertex attribute elements must fit within their declared stride.");
		}
		return {};
	}

	FArdaRHIStatus Validate(const FArdaRHIBindingLayoutDesc& V) noexcept
	{
		if (V.mVisibility == EArdaRHIShaderStage::None || V.mItems.empty())
		{
			return Invalid("Binding layout visibility and items are required.");
		}
		for (size_t I = 0; I < V.mItems.size(); ++I)
		{
			if (V.mItems[I].mArraySize == 0 || V.mItems[I].mArraySize > 65535)
			{
				return Invalid("Binding array size must be between 1 and 65535.");
			}
			for (size_t J = I + 1; J < V.mItems.size(); ++J)
			{
				if (V.mItems[I].mSlot == V.mItems[J].mSlot && V.mItems[I].mType == V.mItems[J].mType)
				{
					return Invalid("Binding layout contains a duplicate slot and type.");
				}
			}
		}
		return {};
	}

	FArdaRHIStatus Validate(const FArdaRHIInputLayoutDesc& V)
	{
		if (V.mAttributes.empty())
		{
			return Invalid("Input layout requires attributes.");
		}
		for (const auto& A : V.mAttributes)
		{
			if (auto S = Validate(A); !S)
			{
				return S;
			}
			for (const auto& B : V.mAttributes)
			{
				if (A.mBufferIndex == B.mBufferIndex &&
				    (A.mElementStride != B.mElementStride || A.mbInstanced != B.mbInstanced))
				{
					return Invalid("Attributes sharing a vertex binding must agree on stride and instance rate.");
				}
			}
		}
		return {};
	}

	FArdaRHIStatus ResolveArdaRHIFramebufferAttachment(const FArdaRHITextureDesc& Texture,
	    const FArdaRHIFramebufferAttachment& Attachment,
	    bool bDepthAttachment,
	    FArdaRHIFramebufferAttachment& Out) noexcept
	{
		Out = {};
		if (auto Status = Validate(Texture); !Status)
		{
			return Status;
		}
		const auto RequiredUsage =
		    bDepthAttachment ? EArdaRHITextureUsage::DepthStencil : EArdaRHITextureUsage::RenderTarget;
		const auto Format = Attachment.mFormat == EArdaRHIFormat::Unknown ? Texture.mFormat : Attachment.mFormat;
		if (!HasAnyFlags(Texture.mUsage, RequiredUsage) || !IsArdaRHITextureViewFormatCompatible(Texture, Format) ||
		    GetArdaRHIFormatInfo(Format).mbDepth != bDepthAttachment || (!bDepthAttachment && Attachment.mbReadOnly))
		{
			return Invalid("Framebuffer attachment usage, format, or read-only mode is incompatible with its slot.");
		}
		auto Range = Attachment.mSubresources;
		if (Range.mMipLevelCount == ArdaRHIAllSubresources)
		{
			Range.mMipLevelCount = 1;
		}
		const auto Fits = [](uint32_t Base, uint32_t Count, uint32_t Limit)
		{
			return Base < Limit && Count && (Count == ArdaRHIAllSubresources || Count <= Limit - Base);
		};
		const uint32_t Planes = GetArdaRHIFormatPlaneCount(Texture.mFormat);
		if (Range.mMipLevelCount != 1 || !Fits(Range.mBaseMipLevel, Range.mMipLevelCount, Texture.mMipLevels) ||
		    !Fits(Range.mBaseArraySlice, Range.mArraySliceCount, Texture.mArraySize) ||
		    !Fits(Range.mBasePlane, Range.mPlaneCount, Planes))
		{
			return Invalid("Framebuffer attachment must select one mip and nonempty in-range layers and aspects.");
		}
		Range = Range.Resolve(Texture);
		if (bDepthAttachment && Range.mBasePlane != 0)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Stencil-only framebuffer attachments are unsupported.");
		}
		Out = Attachment;
		Out.mFormat = Format;
		Out.mSubresources = Range;
		return {};
	}

	FArdaRHIStatus Validate(const FArdaRHIFramebufferDesc& V)
	{
		if ((V.mColorAttachments.empty() && !V.mDepthAttachment.mTexture) ||
		    V.mColorAttachments.size() > ArdaRHIMaxRenderTargets)
		{
			return Invalid("Framebuffer requires attachments within the portable color-attachment limit.");
		}
		uint32_t SampleCount = 0;
		const auto ValidateTarget = [&](const FArdaRHIFramebufferTarget& Target, bool bDepthAttachment)
		{
			if (!Target.mTexture)
			{
				return Invalid("Framebuffer attachment texture is missing.");
			}
			const auto& Texture = Target.mTexture->GetDesc();
			FArdaRHIFramebufferAttachment Resolved;
			if (auto Status =
			        ResolveArdaRHIFramebufferAttachment(Texture, Target.mAttachment, bDepthAttachment, Resolved);
			    !Status)
			{
				return Status;
			}
			if (SampleCount && Texture.mSampleCount != SampleCount)
			{
				return Invalid("Framebuffer attachments must have matching sample counts.");
			}
			SampleCount = Texture.mSampleCount;
			return FArdaRHIStatus{};
		};
		for (const auto& Target : V.mColorAttachments)
		{
			if (auto Status = ValidateTarget(Target, false); !Status)
			{
				return Status;
			}
		}
		return V.mDepthAttachment.mTexture ? ValidateTarget(V.mDepthAttachment, true) : FArdaRHIStatus{};
	}

	FArdaRHIStatus Validate(const FArdaRHIGraphicsPipelineDesc& V)
	{
		if (!V.mVertexShader)
		{
			return Invalid("Graphics pipeline requires a vertex shader.");
		}
		if (V.mVertexShader->GetStage() != EArdaRHIShaderStage::Vertex)
		{
			return Invalid("Graphics pipeline vertex shader has the wrong stage.");
		}
		if (V.mHullShader && V.mHullShader->GetStage() != EArdaRHIShaderStage::Hull)
		{
			return Invalid("Graphics pipeline hull shader has the wrong stage.");
		}
		if (V.mDomainShader && V.mDomainShader->GetStage() != EArdaRHIShaderStage::Domain)
		{
			return Invalid("Graphics pipeline domain shader has the wrong stage.");
		}
		if (V.mGeometryShader && V.mGeometryShader->GetStage() != EArdaRHIShaderStage::Geometry)
		{
			return Invalid("Graphics pipeline geometry shader has the wrong stage.");
		}
		if (V.mPixelShader && V.mPixelShader->GetStage() != EArdaRHIShaderStage::Pixel)
		{
			return Invalid("Graphics pipeline pixel shader has the wrong stage.");
		}
		if (V.mSampleCount == 0 || V.mColorFormats.size() > ArdaRHIMaxRenderTargets)
		{
			return Invalid("Graphics pipeline sample count and attachment formats are invalid.");
		}
		if (V.mTopology == EArdaRHIPrimitiveTopology::PatchList && V.mPatchControlPoints == 0)
		{
			return Invalid("Patch-list pipelines require control points.");
		}
		return {};
	}

	FArdaRHIStatus Validate(const FArdaRHIComputePipelineDesc& V)
	{
		if (!V.mComputeShader)
		{
			return Invalid("Compute pipeline requires a compute shader.");
		}
		return V.mComputeShader->GetStage() == EArdaRHIShaderStage::Compute
		    ? FArdaRHIStatus{}
		    : Invalid("Compute pipeline shader has the wrong stage.");
	}

	FArdaRHIStatus Validate(const FArdaRHIMeshletPipelineDesc& V)
	{
		if (!V.mMeshShader)
		{
			return Invalid("Meshlet pipeline requires a mesh shader.");
		}
		if (V.mMeshShader->GetStage() != EArdaRHIShaderStage::Mesh)
		{
			return Invalid("Meshlet pipeline mesh shader has the wrong stage.");
		}
		if (V.mAmplificationShader && V.mAmplificationShader->GetStage() != EArdaRHIShaderStage::Amplification)
		{
			return Invalid("Meshlet pipeline amplification shader has the wrong stage.");
		}
		if (V.mPixelShader && V.mPixelShader->GetStage() != EArdaRHIShaderStage::Pixel)
		{
			return Invalid("Meshlet pipeline pixel shader has the wrong stage.");
		}
		if (V.mSampleCount == 0 || V.mColorFormats.size() > ArdaRHIMaxRenderTargets)
		{
			return Invalid("Meshlet pipeline sample count and attachment formats are invalid.");
		}
		return {};
	}

	FArdaRHIStatus Validate(const FArdaRHIRayTracingPipelineDesc& V)
	{
		if (V.mShaders.empty() || V.mMaxRecursionDepth == 0)
		{
			return Invalid("Ray-tracing pipelines require shaders and non-zero recursion depth.");
		}
		for (const auto& S : V.mShaders)
		{
			if (S.mExportName.empty() || !S.mShader ||
			    !HasAnyFlags(EArdaRHIShaderStage::AllRayTracing, S.mShader->GetStage()))
			{
				return Invalid("Ray-tracing pipeline shader exports require a name and ray-tracing shader.");
			}
		}
		for (const auto& H : V.mHitGroups)
		{
			if (H.mExportName.empty() || (!H.mClosestHitShader && !H.mAnyHitShader && !H.mIntersectionShader))
			{
				return Invalid("Ray-tracing hit groups require a name and at least one shader.");
			}
			if (H.mbProceduralPrimitive && !H.mIntersectionShader)
			{
				return Invalid("Procedural ray-tracing hit groups require an intersection shader.");
			}
		}
		return {};
	}
}
