#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	TArdaRHIResult<eastl::vector<FArdaProviderRayTracingGeometry>> ResolveRayTracingGeometries(
	    FArdaRHIDeviceImpl& Device,
	    const eastl::vector<FArdaRHIRayTracingGeometryDesc>& Geometries)
	{
		eastl::vector<FArdaProviderRayTracingGeometry> Native;
		Native.reserve(Geometries.size());
		for (const auto& Geometry : Geometries)
		{
			FArdaProviderRayTracingGeometry Resolved;
			Resolved.mDesc = Geometry;
			if (Geometry.mIndexBuffer)
			{
				auto* Buffer = Cast<FArdaBuffer>(Geometry.mIndexBuffer.Get());
				if (!Buffer || !Device.Owns(Buffer))
				{
					return Failure<eastl::vector<FArdaProviderRayTracingGeometry>>(WrongDevice());
				}
				Resolved.mIndexBuffer = Buffer->mNative;
			}
			if (Geometry.mVertexOrAABBBuffer)
			{
				auto* Buffer = Cast<FArdaBuffer>(Geometry.mVertexOrAABBBuffer.Get());
				if (!Buffer || !Device.Owns(Buffer))
				{
					return Failure<eastl::vector<FArdaProviderRayTracingGeometry>>(WrongDevice());
				}
				Resolved.mVertexOrAABBBuffer = Buffer->mNative;
			}
			if (Geometry.mOpacityMicromapIndexBuffer)
			{
				auto* Buffer = Cast<FArdaBuffer>(Geometry.mOpacityMicromapIndexBuffer.Get());
				if (!Buffer || !Device.Owns(Buffer))
				{
					return Failure<eastl::vector<FArdaProviderRayTracingGeometry>>(WrongDevice());
				}
				Resolved.mOpacityMicromapIndexBuffer = Buffer->mNative;
			}
			if (Geometry.mOpacityMicromap)
			{
				auto* Micromap = Cast<FArdaOpacityMicromap>(Geometry.mOpacityMicromap.Get());
				if (!Micromap || !Device.Owns(Micromap))
				{
					return Failure<eastl::vector<FArdaProviderRayTracingGeometry>>(WrongDevice());
				}
				Resolved.mOpacityMicromap = Micromap->mNative;
			}

			// Facade references are intentionally stripped at the
			// provider-neutral/native boundary.
			Resolved.mDesc.mIndexBuffer = {};
			Resolved.mDesc.mVertexOrAABBBuffer = {};
			Resolved.mDesc.mOpacityMicromap = {};
			Resolved.mDesc.mOpacityMicromapIndexBuffer = {};
			Native.push_back(eastl::move(Resolved));
		}
		return {eastl::move(Native), {}};
	}

	TArdaRHIResult<FArdaRHITextureRef> FArdaRHIDeviceImpl::CreateTexture(const FArdaRHITextureDesc& Desc)
	{
		if (Desc.mbCudaInterop)
		{
			if ((Desc.mDimension == EArdaRHITextureDimension::Texture1DArray ||
			        Desc.mDimension == EArdaRHITextureDimension::Texture2DArray) &&
			    !GetCudaCapabilities().mbLayeredSurfaceAccess)
			{
				return Failure<FArdaRHITextureRef>(
				    Unsupported("Layered CUDA surfaces are not qualified in this execution mode."));
			}
			if (!GetCudaCapabilities())
			{
				return UnsupportedResult<FArdaRHITextureRef>(GetCudaCapabilities().mUnavailableReason.c_str());
			}
			if (!GetCudaCapabilities().mbSurfaceAccess)
			{
				return UnsupportedResult<FArdaRHITextureRef>(
				    GetCudaCapabilities().mSurfaceUnavailableReason.c_str());
			}
			if (auto Status = ValidateArdaCudaTexture(Desc); !Status)
			{
				return Failure<FArdaRHITextureRef>(Status);
			}
		}
		if (auto Status = ValidateResourceCapabilities(Desc, GetCapabilities(), QueryFormatSupport(Desc.mFormat));
		    !Status)
		{
			return Failure<FArdaRHITextureRef>(eastl::move(Status));
		}
		if (Desc.mbTiled &&
		    !(mDevice->GetCapabilities().mResidency.mbReservedTexture2D ||
		        mDevice->GetCapabilities().mResidency.mbReservedTexture3D))
		{
			return UnsupportedResult<FArdaRHITextureRef>(
			    "Tiled textures are unsupported by the backend providers.");
		}
		auto Native = mAllocator.CreateTexture(Desc);
		if (!Native)
		{
			return Failure<FArdaRHITextureRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHITextureRef(new FArdaTexture(Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {}};
	}

	TArdaRHIResult<FArdaRHITextureReferenceRef> FArdaRHIDeviceImpl::CreateTextureReference(
	    const FArdaRHITextureRef& Texture)
	{
		if (!IsOwned(Texture))
		{
			return Failure<FArdaRHITextureReferenceRef>(WrongDevice());
		}
		return {FArdaRHITextureReferenceRef(new FArdaTextureReference(Texture, this, mLifetimeTracker)), {}};
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::SetTextureReference(const FArdaRHITextureReferenceRef& Reference,
	    const FArdaRHITextureRef& Texture)
	{
		auto* Native = Cast<FArdaTextureReference>(Reference.Get());
		if (!Native || !Owns(Native) || !IsOwned(Texture))
		{
			return WrongDevice();
		}
		Native->mTexture = Texture;
		return {};
	}

	TArdaRHIResult<FArdaRHIBufferRef> FArdaRHIDeviceImpl::CreateBuffer(const FArdaRHIBufferDesc& Desc)
	{
		if (Desc.mbCudaInterop)
		{
			if (!GetCudaCapabilities())
			{
				return UnsupportedResult<FArdaRHIBufferRef>(GetCudaCapabilities().mUnavailableReason.c_str());
			}
			if (auto Status = ValidateArdaCudaBuffer(Desc); !Status)
			{
				return Failure<FArdaRHIBufferRef>(Status);
			}
		}
		if (auto Status = ValidateResourceCapabilities(Desc, GetCapabilities(), QueryFormatSupport(Desc.mFormat));
		    !Status)
		{
			return Failure<FArdaRHIBufferRef>(eastl::move(Status));
		}
		if (Desc.mbTiled && !mDevice->GetCapabilities().mResidency.mbReservedBuffers)
		{
			return UnsupportedResult<FArdaRHIBufferRef>("Sparse buffers are unsupported by this device.");
		}
		auto Native = mAllocator.CreateBuffer(Desc);
		if (!Native)
		{
			return Failure<FArdaRHIBufferRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIBufferRef(new FArdaBuffer(Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {}};
	}

	TArdaRHIResult<FArdaRHIUniformBufferRef> FArdaRHIDeviceImpl::CreateUniformBuffer(
	    const FArdaRHIUniformBufferDesc& Desc,
	    const void* InitialData)
	{
		if (Desc.mByteSize == 0)
		{
			return Failure<FArdaRHIUniformBufferRef>(Invalid("Uniform buffer size must be non-zero."));
		}
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = Desc.mByteSize;
		BufferDesc.mMaxVersions = Desc.mMaxVersions;
		BufferDesc.mUsage = EArdaRHIBufferUsage::Constant;
		BufferDesc.mInitialState = EArdaRHIResourceState::ConstantBuffer;
		BufferDesc.mbKeepInitialState = true;
		BufferDesc.mDebugName = Desc.mDebugName;
		auto Buffer = CreateBuffer(BufferDesc);
		if (!Buffer)
		{
			return Failure<FArdaRHIUniformBufferRef>(eastl::move(Buffer.mStatus));
		}
		if (InitialData)
		{
			auto Commands = CreateCommandList(EArdaRHIQueueType::Graphics, true);
			if (!Commands)
			{
				return Failure<FArdaRHIUniformBufferRef>(eastl::move(Commands.mStatus));
			}
			if (auto Status = Commands.mValue->Open(); !Status)
			{
				return Failure<FArdaRHIUniformBufferRef>(eastl::move(Status));
			}
			if (auto Status = Commands.mValue->WriteBuffer(*Buffer.mValue, InitialData, Desc.mByteSize); !Status)
			{
				return Failure<FArdaRHIUniformBufferRef>(eastl::move(Status));
			}
			if (auto Status = Commands.mValue->Close(); !Status)
			{
				return Failure<FArdaRHIUniformBufferRef>(eastl::move(Status));
			}
			auto Submitted = ExecuteCommandList(Commands.mValue);
			if (!Submitted)
			{
				return Failure<FArdaRHIUniformBufferRef>(eastl::move(Submitted.mStatus));
			}
		}
		return {FArdaRHIUniformBufferRef(new FArdaUniformBuffer(Desc, Buffer.mValue, this, mLifetimeTracker)), {}};
	}

	TArdaRHIResult<FArdaRHIStagingTextureRef> FArdaRHIDeviceImpl::CreateStagingTexture(
	    const FArdaRHIStagingTextureDesc& Desc)
	{
		if (auto Status = Validate(Desc.mTexture); !Status)
		{
			return Failure<FArdaRHIStagingTextureRef>(eastl::move(Status));
		}
		if (Desc.mCpuAccess == EArdaRHICpuAccess::None)
		{
			return Failure<FArdaRHIStagingTextureRef>(Invalid("A staging texture requires CPU access."));
		}
		auto Native = mDevice->CreateStagingTexture(Desc);
		if (!Native)
		{
			return Failure<FArdaRHIStagingTextureRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIStagingTextureRef(
		            new FArdaStagingTexture(Desc, eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHIStagingTextureMapping> FArdaRHIDeviceImpl::MapStagingTexture(
	    const FArdaRHIStagingTextureRef& Texture,
	    const FArdaRHITextureSlice& Slice,
	    EArdaRHICpuAccess Access)
	{
		auto* Native = Cast<FArdaStagingTexture>(Texture.Get());
		if (!Native || !Owns(Native))
		{
			return Failure<FArdaRHIStagingTextureMapping>(WrongDevice());
		}
		if (Access == EArdaRHICpuAccess::None || Access != Native->mDesc.mCpuAccess)
		{
			return Failure<FArdaRHIStagingTextureMapping>(
			    Invalid("Staging texture mapping access does not match its descriptor."));
		}
		return mDevice->MapStagingTexture(Native->mNative, Slice, Access);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::UnmapStagingTexture(const FArdaRHIStagingTextureRef& Texture)
	{
		auto* Native = Cast<FArdaStagingTexture>(Texture.Get());
		if (!Native || !Owns(Native))
		{
			return WrongDevice();
		}
		return mDevice->UnmapStagingTexture(Native->mNative);
	}

	TArdaRHIResult<FArdaRHIShaderResourceViewRef> FArdaRHIDeviceImpl::CreateShaderResourceView(
	    const TArdaRHIRef<IArdaRHIResource>& Resource,
	    const FArdaRHIViewDesc& Desc)
	{
		auto* Native = Cast<FArdaResource>(Resource.Get());
		if (!Native || !Owns(Native) || (!Cast<FArdaTexture>(Resource.Get()) && !Cast<FArdaBuffer>(Resource.Get())))
		{
			return Failure<FArdaRHIShaderResourceViewRef>(WrongDevice());
		}
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHIShaderResourceViewRef>(eastl::move(Status));
		}
		return {FArdaRHIShaderResourceViewRef(new FArdaShaderResourceView(Resource, Desc, this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHIUnorderedAccessViewRef> FArdaRHIDeviceImpl::CreateUnorderedAccessView(
	    const TArdaRHIRef<IArdaRHIResource>& Resource,
	    const FArdaRHIViewDesc& Desc)
	{
		auto* Native = Cast<FArdaResource>(Resource.Get());
		if (!Native || !Owns(Native) || (!Cast<FArdaTexture>(Resource.Get()) && !Cast<FArdaBuffer>(Resource.Get())))
		{
			return Failure<FArdaRHIUnorderedAccessViewRef>(WrongDevice());
		}
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHIUnorderedAccessViewRef>(eastl::move(Status));
		}
		return {
		    FArdaRHIUnorderedAccessViewRef(new FArdaUnorderedAccessView(Resource, Desc, this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHISamplerFeedbackTextureRef> FArdaRHIDeviceImpl::CreateSamplerFeedbackTexture(
	    const FArdaRHITextureRef& PairedTextureRef,
	    const FArdaRHISamplerFeedbackTextureDesc& InputDesc)
	{
		if (GetCapabilities().mSamplerFeedbackTier == EArdaRHISamplerFeedbackTier::None)
		{
			return UnsupportedResult<FArdaRHISamplerFeedbackTextureRef>(
			    "Sampler feedback is unsupported by this device.");
		}
		auto* PairedTexture = Cast<FArdaTexture>(PairedTextureRef.Get());
		if (!PairedTexture || !Owns(PairedTexture))
		{
			return Failure<FArdaRHISamplerFeedbackTextureRef>(WrongDevice());
		}
		if (PairedTexture->mDesc.mDimension != EArdaRHITextureDimension::Texture2D ||
		    PairedTexture->mDesc.mSampleCount != 1)
		{
			return Failure<FArdaRHISamplerFeedbackTextureRef>(
			    Invalid("Sampler feedback requires a non-multisampled 2D paired texture."));
		}
		FArdaRHISamplerFeedbackTextureDesc Desc = InputDesc;
		Desc.mMipRegionX = Desc.mMipRegionX ? Desc.mMipRegionX : 4u;
		Desc.mMipRegionY = Desc.mMipRegionY ? Desc.mMipRegionY : 4u;
		Desc.mMipRegionZ = Desc.mMipRegionZ ? Desc.mMipRegionZ : 1u;
		const auto ValidRegion = [](uint32_t Size, uint32_t Extent)
		{
			return Size >= 4 && (Size & (Size - 1)) == 0 && Size <= Extent / 2;
		};
		if (!ValidRegion(Desc.mMipRegionX, PairedTexture->mDesc.mWidth) ||
		    !ValidRegion(Desc.mMipRegionY, PairedTexture->mDesc.mHeight) || Desc.mMipRegionZ != 1)
		{
			return Failure<FArdaRHISamplerFeedbackTextureRef>(Invalid(
			    "Feedback regions must be powers of two from four to half the paired extent; depth must be one."));
		}
		Desc.mInitialState = Desc.mInitialState == EArdaRHIResourceState::Unknown
		    ? EArdaRHIResourceState::UnorderedAccess
		    : Desc.mInitialState;
		auto Native = mDevice->CreateSamplerFeedbackTexture(PairedTexture->mNative, PairedTexture->mDesc, Desc);
		if (!Native)
		{
			return Failure<FArdaRHISamplerFeedbackTextureRef>(eastl::move(Native.mStatus));
		}
		auto* Feedback = new FArdaSamplerFeedbackTexture(Desc,
		    PairedTextureRef,
		    eastl::move(Native.mValue),
		    this,
		    mLifetimeTracker);
		Feedback->mFacadeState = Desc.mInitialState;
		Feedback->mbFacadeStateKnown = true;
		return {FArdaRHISamplerFeedbackTextureRef(Feedback), {}};
	}

	TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements> FArdaRHIDeviceImpl::GetAccelStructBuildMemoryRequirements(
	    const FArdaRHIAccelStructDesc& Desc)
	{
		if (!mDevice->GetCapabilities().mRayTracing.mbAccelerationStructures)
		{
			return UnsupportedResult<FArdaRHIAccelStructMemoryRequirements>(
			    "Acceleration structures are unsupported by this device.");
		}
		if (Desc.mbTopLevel == !Desc.mBottomLevelGeometries.empty())
		{
			return Failure<FArdaRHIAccelStructMemoryRequirements>(
			    Invalid("An acceleration structure must be either TLAS or BLAS."));
		}
		if (Desc.mbTopLevel && !Desc.mTopLevelMaxInstances)
		{
			return Failure<FArdaRHIAccelStructMemoryRequirements>(
			    Invalid("A TLAS requires a non-zero maximum instance count."));
		}
		auto Geometries = ResolveRayTracingGeometries(*this, Desc.mBottomLevelGeometries);
		if (!Geometries)
		{
			return Failure<FArdaRHIAccelStructMemoryRequirements>(eastl::move(Geometries.mStatus));
		}
		return mDevice->GetAccelStructBuildMemoryRequirements(Desc, Geometries.mValue);
	}

	TArdaRHIResult<FArdaRHIAccelStructRef> FArdaRHIDeviceImpl::CreateAccelStruct(
	    const FArdaRHIAccelStructDesc& Desc)
	{
		auto Requirements = GetAccelStructBuildMemoryRequirements(Desc);
		if (!Requirements)
		{
			return Failure<FArdaRHIAccelStructRef>(eastl::move(Requirements.mStatus));
		}
		auto Native = mDevice->CreateAccelStruct(Desc, Requirements.mValue);
		if (!Native)
		{
			return Failure<FArdaRHIAccelStructRef>(eastl::move(Native.mStatus));
		}
		const uint64_t Address = mDevice->GetAccelStructDeviceAddress(Native.mValue);
		if (!Address && !Desc.mbVirtual)
		{
			return Failure<FArdaRHIAccelStructRef>(FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			    "The native acceleration structure has no device address."));
		}
		return {FArdaRHIAccelStructRef(new FArdaAccelStruct(Desc,
		            Requirements.mValue,
		            eastl::move(Native.mValue),
		            Address,
		            this,
		            mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHIOpacityMicromapRef> FArdaRHIDeviceImpl::CreateOpacityMicromap(
	    const FArdaRHIOpacityMicromapDesc& Desc)
	{
		if (!mDevice->GetCapabilities().mRayTracing.mbOpacityMicromaps)
		{
			return UnsupportedResult<FArdaRHIOpacityMicromapRef>(
			    "Opacity micromaps are unsupported by this device.");
		}
		if (Desc.mCounts.empty() || !Desc.mInputBuffer || !Desc.mPerMicromapDescBuffer)
		{
			return Failure<FArdaRHIOpacityMicromapRef>(Invalid(
			    "An opacity micromap requires usage counts, encoded data, and per-micromap triangle descriptors."));
		}
		if (!Desc.mbTrackLiveness && !Desc.mbAllowUnsafeLivenessOptOut)
		{
			return Failure<FArdaRHIOpacityMicromapRef>(
			    Invalid("Disabling opacity-micromap liveness tracking requires an explicit unsafe opt-out."));
		}
		for (const auto& Count : Desc.mCounts)
		{
			if (!Count.mCount)
			{
				return Failure<FArdaRHIOpacityMicromapRef>(
				    Invalid("Opacity-micromap usage counts must be non-zero."));
			}
		}
		auto* Input = Cast<FArdaBuffer>(Desc.mInputBuffer.Get());
		auto* Triangles = Cast<FArdaBuffer>(Desc.mPerMicromapDescBuffer.Get());
		if (!Input || !Triangles || !Owns(Input) || !Owns(Triangles))
		{
			return Failure<FArdaRHIOpacityMicromapRef>(WrongDevice());
		}
		if (Desc.mInputBufferOffset >= Input->mDesc.mByteSize ||
		    Desc.mPerMicromapDescBufferOffset >= Triangles->mDesc.mByteSize || Desc.mInputBufferOffset % 256u ||
		    Desc.mPerMicromapDescBufferOffset % 256u)
		{
			return Failure<FArdaRHIOpacityMicromapRef>(
			    Invalid("Opacity-micromap input offsets must be in range and 256-byte aligned."));
		}
		if (!HasAnyFlags(Input->mDesc.mUsage, EArdaRHIBufferUsage::OpacityMicromapBuildInput) ||
		    !HasAnyFlags(Triangles->mDesc.mUsage, EArdaRHIBufferUsage::OpacityMicromapBuildInput))
		{
			return Failure<FArdaRHIOpacityMicromapRef>(
			    Invalid("Opacity-micromap inputs require OpacityMicromapBuildInput buffer usage."));
		}
		FArdaRHIOpacityMicromapDesc NativeDesc = Desc;
		NativeDesc.mInputBuffer = {};
		NativeDesc.mPerMicromapDescBuffer = {};
		auto Native = mDevice->CreateOpacityMicromap(NativeDesc, Input->mNative, Triangles->mNative);
		if (!Native)
		{
			return Failure<FArdaRHIOpacityMicromapRef>(eastl::move(Native.mStatus));
		}
		const uint64_t Address = mDevice->GetOpacityMicromapDeviceAddress(Native.mValue);
		if (!Address)
		{
			return Failure<FArdaRHIOpacityMicromapRef>(FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			    "The native opacity micromap has no storage address."));
		}
		return {FArdaRHIOpacityMicromapRef(
		            new FArdaOpacityMicromap(Desc, eastl::move(Native.mValue), Address, this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<uint64_t> FArdaRHIDeviceImpl::GetAccelStructCompactedSize(const FArdaRHIAccelStructRef& Ref)
	{
		auto* AccelStruct = Cast<FArdaAccelStruct>(Ref.Get());
		if (!AccelStruct || !Owns(AccelStruct))
		{
			return Failure<uint64_t>(WrongDevice());
		}
		if (!HasAnyFlags(AccelStruct->mDesc.mBuildFlags, EArdaRHIAccelStructBuildFlags::AllowCompaction))
		{
			return Failure<uint64_t>(Invalid("Compacted size requires AllowCompaction at creation."));
		}
		if (AccelStruct->GetBuildState() == EArdaRHIAccelStructBuildState::Unbuilt)
		{
			return Failure<uint64_t>(FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Compacted size is unavailable before a successful build."));
		}
		return mDevice->GetAccelStructCompactedSize(AccelStruct->mNative);
	}

	TArdaRHIResult<uint64_t> FArdaRHIDeviceImpl::GetOpacityMicromapCompactedSize(
	    const FArdaRHIOpacityMicromapRef& Ref)
	{
		auto* Micromap = Cast<FArdaOpacityMicromap>(Ref.Get());
		if (!Micromap || !Owns(Micromap))
		{
			return Failure<uint64_t>(WrongDevice());
		}
		if (!HasAnyFlags(Micromap->mDesc.mFlags, EArdaRHIOpacityMicromapBuildFlags::AllowCompaction))
		{
			return Failure<uint64_t>(Invalid("Compacted size requires AllowCompaction at creation."));
		}
		if (Micromap->GetBuildState() == EArdaRHIAccelStructBuildState::Unbuilt)
		{
			return Failure<uint64_t>(FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Compacted size is unavailable before a successful micromap build."));
		}
		return mDevice->GetOpacityMicromapCompactedSize(Micromap->mNative);
	}

	TArdaRHIResult<FArdaRHIMemoryRequirements> FArdaRHIDeviceImpl::GetAccelStructMemoryRequirements(
	    const FArdaRHIAccelStructRef& Ref)
	{
		auto* AccelStruct = Cast<FArdaAccelStruct>(Ref.Get());
		if (!AccelStruct || !Owns(AccelStruct))
		{
			return Failure<FArdaRHIMemoryRequirements>(WrongDevice());
		}
		FArdaRHIMemoryRequirements Result;
		Result.mSize = AccelStruct->mRequirements.mResultSize;
		Result.mAlignment = AccelStruct->mRequirements.mResultAlignment;
		return {Result, {}};
	}
}
