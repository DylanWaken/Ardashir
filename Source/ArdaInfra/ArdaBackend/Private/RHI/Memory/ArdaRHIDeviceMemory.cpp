#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	TArdaRHIResult<FArdaRHIHeapRef> FArdaRHIDeviceImpl::CreateHeap(const FArdaRHIHeapDesc& Desc)
	{
		if (!Desc.mCapacity || !Desc.mMemoryTypeBits)
		{
			return Failure<FArdaRHIHeapRef>(
			    Invalid("A heap requires non-zero capacity and compatible memory types."));
		}
		auto Native = mAllocator.CreateHeap(Desc);
		if (!Native)
		{
			return Failure<FArdaRHIHeapRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIHeapRef(new FArdaHeap(Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {}};
	}

	FArdaRHIStatus ValidateHeapPlacement(const FArdaRHIMemoryRequirements& Requirements,
	    const FArdaRHIHeapDesc& Heap,
	    uint64_t Offset)
	{
		if (!Requirements.mAlignment || Offset % Requirements.mAlignment != 0 || Offset > Heap.mCapacity ||
		    Requirements.mSize > Heap.mCapacity - Offset || !(Requirements.mMemoryTypeBits & Heap.mMemoryTypeBits))
		{
			return Invalid("Heap placement does not satisfy size, alignment, or memory-type requirements.");
		}
		return {};
	}

	TArdaRHIResult<FArdaRHIBufferRef> FArdaRHIDeviceImpl::CreatePlacedBuffer(const FArdaRHIBufferDesc& Desc,
	    const FArdaRHIHeapRef& Heap,
	    uint64_t Offset)
	{
		auto* NativeHeap = Cast<FArdaHeap>(Heap.Get());
		if (!NativeHeap || !Owns(NativeHeap))
		{
			return Failure<FArdaRHIBufferRef>(WrongDevice());
		}
		if (!Desc.mbVirtual)
		{
			return Failure<FArdaRHIBufferRef>(Invalid("Placed buffers require a virtual descriptor."));
		}
		const auto Requirements = QueryBufferMemoryRequirements(Desc);
		if (!Requirements)
		{
			return Failure<FArdaRHIBufferRef>(Requirements.mStatus);
		}
		if (auto Status = ValidateHeapPlacement(Requirements.mValue, NativeHeap->mDesc, Offset); !Status)
		{
			return Failure<FArdaRHIBufferRef>(Status);
		}
		auto Native = mAllocator.CreatePlacedBuffer(Desc, NativeHeap->mNative, Offset);
		if (!Native)
		{
			return Failure<FArdaRHIBufferRef>(Native.mStatus);
		}
		auto* Buffer = new FArdaBuffer(Desc, eastl::move(Native.mValue), this, mLifetimeTracker);
		Buffer->mHeap = Heap;
		Buffer->mHeapOffset = Offset;
		return {FArdaRHIBufferRef(Buffer), {}};
	}

	TArdaRHIResult<FArdaRHITextureRef> FArdaRHIDeviceImpl::CreatePlacedTexture(const FArdaRHITextureDesc& Desc,
	    const FArdaRHIHeapRef& Heap,
	    uint64_t Offset)
	{
		auto* NativeHeap = Cast<FArdaHeap>(Heap.Get());
		if (!NativeHeap || !Owns(NativeHeap))
		{
			return Failure<FArdaRHITextureRef>(WrongDevice());
		}
		if (!Desc.mbVirtual)
		{
			return Failure<FArdaRHITextureRef>(Invalid("Placed textures require a virtual descriptor."));
		}
		const auto Requirements = QueryTextureMemoryRequirements(Desc);
		if (!Requirements)
		{
			return Failure<FArdaRHITextureRef>(Requirements.mStatus);
		}
		if (auto Status = ValidateHeapPlacement(Requirements.mValue, NativeHeap->mDesc, Offset); !Status)
		{
			return Failure<FArdaRHITextureRef>(Status);
		}
		auto Native = mAllocator.CreatePlacedTexture(Desc, NativeHeap->mNative, Offset);
		if (!Native)
		{
			return Failure<FArdaRHITextureRef>(Native.mStatus);
		}
		auto* Texture = new FArdaTexture(Desc, eastl::move(Native.mValue), this, mLifetimeTracker);
		Texture->mHeap = Heap;
		Texture->mHeapOffset = Offset;
		return {FArdaRHITextureRef(Texture), {}};
	}

	TArdaRHIResult<FArdaRHIMemoryRequirements> FArdaRHIDeviceImpl::QueryTextureMemoryRequirements(
	    const FArdaRHITextureDesc& Desc)
	{
		if (auto Status = ValidateResourceCapabilities(Desc, GetCapabilities(), QueryFormatSupport(Desc.mFormat));
		    !Status)
		{
			return Failure<FArdaRHIMemoryRequirements>(eastl::move(Status));
		}
		if (Desc.mbVirtual && Desc.mbTiled)
		{
			return Failure<FArdaRHIMemoryRequirements>(Invalid("A texture cannot be both virtual and tiled."));
		}
		if (Desc.mbCudaInterop)
		{
			if (auto Status = ValidateArdaCudaTexture(Desc); !Status)
			{
				return Failure<FArdaRHIMemoryRequirements>(eastl::move(Status));
			}
			const auto Cuda = GetCudaCapabilities();
			if (!Cuda)
			{
				return UnsupportedResult<FArdaRHIMemoryRequirements>(Cuda.mUnavailableReason.c_str());
			}
			if (!Cuda.mbSurfaceAccess)
			{
				return UnsupportedResult<FArdaRHIMemoryRequirements>(Cuda.mSurfaceUnavailableReason.c_str());
			}
			if ((Desc.mDimension == EArdaRHITextureDimension::Texture1DArray ||
			        Desc.mDimension == EArdaRHITextureDimension::Texture2DArray) &&
			    !Cuda.mbLayeredSurfaceAccess)
			{
				return UnsupportedResult<FArdaRHIMemoryRequirements>(
				    "Layered CUDA surfaces are not qualified in this execution mode.");
			}
		}
		if (Desc.mbTiled &&
		    !(mDevice->GetCapabilities().mResidency.mbReservedTexture2D ||
		        mDevice->GetCapabilities().mResidency.mbReservedTexture3D))
		{
			return UnsupportedResult<FArdaRHIMemoryRequirements>("Tiled textures are unsupported by this device.");
		}
		return mDevice->QueryTextureMemoryRequirements(Desc);
	}

	TArdaRHIResult<FArdaRHIMemoryRequirements> FArdaRHIDeviceImpl::QueryBufferMemoryRequirements(
	    const FArdaRHIBufferDesc& Desc)
	{
		if (auto Status = ValidateResourceCapabilities(Desc, GetCapabilities(), QueryFormatSupport(Desc.mFormat));
		    !Status)
		{
			return Failure<FArdaRHIMemoryRequirements>(eastl::move(Status));
		}
		if (Desc.mbVirtual && Desc.mbTiled)
		{
			return Failure<FArdaRHIMemoryRequirements>(Invalid("A buffer cannot be both virtual and tiled."));
		}
		if (Desc.mbCudaInterop)
		{
			if (auto Status = ValidateArdaCudaBuffer(Desc); !Status)
			{
				return Failure<FArdaRHIMemoryRequirements>(eastl::move(Status));
			}
			const auto Cuda = GetCudaCapabilities();
			if (!Cuda)
			{
				return UnsupportedResult<FArdaRHIMemoryRequirements>(Cuda.mUnavailableReason.c_str());
			}
		}
		if (Desc.mbTiled && !mDevice->GetCapabilities().mResidency.mbReservedBuffers)
		{
			return UnsupportedResult<FArdaRHIMemoryRequirements>("Sparse buffers are unsupported by this device.");
		}
		return mDevice->QueryBufferMemoryRequirements(Desc);
	}

	TArdaRHIResult<FArdaRHIMemoryRequirements> FArdaRHIDeviceImpl::GetTextureMemoryRequirements(
	    const FArdaRHITextureRef& Texture)
	{
		auto* Native = Cast<FArdaTexture>(Texture.Get());
		if (!Native || !Owns(Native))
		{
			return Failure<FArdaRHIMemoryRequirements>(WrongDevice());
		}
		return mDevice->GetTextureMemoryRequirements(Native->mNative, Native->mDesc);
	}

	TArdaRHIResult<FArdaRHIMemoryRequirements> FArdaRHIDeviceImpl::GetBufferMemoryRequirements(
	    const FArdaRHIBufferRef& Buffer)
	{
		auto* Native = Cast<FArdaBuffer>(Buffer.Get());
		if (!Native || !Owns(Native))
		{
			return Failure<FArdaRHIMemoryRequirements>(WrongDevice());
		}
		return mDevice->GetBufferMemoryRequirements(Native->mNative, Native->mDesc);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::BindTextureMemory(const FArdaRHITextureRef& Texture,
	    const FArdaRHIHeapRef& Heap,
	    uint64_t Offset)
	{
		auto* NativeTexture = Cast<FArdaTexture>(Texture.Get());
		auto* NativeHeap = Cast<FArdaHeap>(Heap.Get());
		if (!NativeTexture || !NativeHeap || !Owns(NativeTexture) || !Owns(NativeHeap))
		{
			return WrongDevice();
		}
		if (!NativeTexture->mDesc.mbVirtual)
		{
			return Invalid("Only virtual textures can be bound to an explicit heap.");
		}
		if (NativeTexture->mHeap)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Virtual texture memory is already bound.");
		}
		const auto Requirements = GetTextureMemoryRequirements(Texture);
		if (!Requirements)
		{
			return Requirements.mStatus;
		}
		if (!Requirements.mValue.mAlignment || Offset % Requirements.mValue.mAlignment != 0 ||
		    Offset > NativeHeap->mDesc.mCapacity ||
		    Requirements.mValue.mSize > NativeHeap->mDesc.mCapacity - Offset ||
		    !(Requirements.mValue.mMemoryTypeBits & NativeHeap->mDesc.mMemoryTypeBits))
		{
			return Invalid("Texture heap binding does not satisfy size, alignment, or memory-type requirements.");
		}
		const FArdaRHIStatus Status =
		    mAllocator.BindTextureMemory(NativeTexture->mNative, NativeTexture->mDesc, NativeHeap->mNative, Offset);
		if (Status)
		{
			NativeTexture->mHeap = Heap;
			NativeTexture->mHeapOffset = Offset;
		}
		return Status;
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::BindBufferMemory(const FArdaRHIBufferRef& Buffer,
	    const FArdaRHIHeapRef& Heap,
	    uint64_t Offset)
	{
		auto* NativeBuffer = Cast<FArdaBuffer>(Buffer.Get());
		auto* NativeHeap = Cast<FArdaHeap>(Heap.Get());
		if (!NativeBuffer || !NativeHeap || !Owns(NativeBuffer) || !Owns(NativeHeap))
		{
			return WrongDevice();
		}
		if (!NativeBuffer->mDesc.mbVirtual)
		{
			return Invalid("Only virtual buffers can be bound to an explicit heap.");
		}
		if (NativeBuffer->mHeap)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Virtual buffer memory is already bound.");
		}
		const auto Requirements = GetBufferMemoryRequirements(Buffer);
		if (!Requirements)
		{
			return Requirements.mStatus;
		}
		if (!Requirements.mValue.mAlignment || Offset % Requirements.mValue.mAlignment != 0 ||
		    Offset > NativeHeap->mDesc.mCapacity ||
		    Requirements.mValue.mSize > NativeHeap->mDesc.mCapacity - Offset ||
		    !(Requirements.mValue.mMemoryTypeBits & NativeHeap->mDesc.mMemoryTypeBits))
		{
			return Invalid("Buffer heap binding does not satisfy size, alignment, or memory-type requirements.");
		}
		const FArdaRHIStatus Status =
		    mAllocator.BindBufferMemory(NativeBuffer->mNative, NativeBuffer->mDesc, NativeHeap->mNative, Offset);
		if (Status)
		{
			NativeBuffer->mHeap = Heap;
			NativeBuffer->mHeapOffset = Offset;
		}
		return Status;
	}

	TArdaRHIResult<FArdaRHITextureTiling> FArdaRHIDeviceImpl::GetTextureTiling(const FArdaRHITextureRef& Texture)
	{
		auto* Native = Cast<FArdaTexture>(Texture.Get());
		if (!Native || !Owns(Native))
		{
			return Failure<FArdaRHITextureTiling>(WrongDevice());
		}
		if (!Native->mDesc.mbTiled)
		{
			return Failure<FArdaRHITextureTiling>(Invalid("Texture tiling is available only for tiled textures."));
		}
		return mDevice->GetTextureTiling(Native->mNative);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::UpdateTextureTileMappings(const FArdaRHITextureRef& Texture,
	    const eastl::vector<FArdaRHITextureTileMapping>& Mappings,
	    EArdaRHIQueueType Queue)
	{
		auto* Native = Cast<FArdaTexture>(Texture.Get());
		if (!Native || !Owns(Native))
		{
			return WrongDevice();
		}
		if (!Native->mDesc.mbTiled)
		{
			return Invalid("Tile mappings require a tiled texture.");
		}
		if (!GetCapabilities().IsQueueSupported(Queue))
		{
			return Unsupported("The requested sparse-binding queue is unavailable.");
		}
		eastl::vector<FArdaProviderTextureTileMapping> Resolved;
		Resolved.reserve(Mappings.size());
		for (const auto& Mapping : Mappings)
		{
			if (Mapping.mCoordinates.size() != Mapping.mRegions.size() ||
			    Mapping.mCoordinates.size() != Mapping.mByteOffsets.size())
			{
				return Invalid("Texture tile coordinates, regions, and heap offsets must have equal counts.");
			}
			FArdaProviderTextureTileMapping Entry;
			Entry.mCoordinates = Mapping.mCoordinates;
			Entry.mRegions = Mapping.mRegions;
			Entry.mByteOffsets = Mapping.mByteOffsets;
			if (Mapping.mHeap)
			{
				auto* Heap = Cast<FArdaHeap>(Mapping.mHeap.Get());
				if (!Heap || !Owns(Heap))
				{
					return WrongDevice();
				}
				Entry.mHeap = Heap->mNative;
			}
			Resolved.push_back(eastl::move(Entry));
		}
		return mDevice->UpdateTextureTileMappings(Native->mNative, Resolved, Queue);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::UpdateBufferTileMappings(const FArdaRHIBufferRef& Buffer,
	    const eastl::vector<FArdaRHIBufferTileMapping>& Mappings,
	    EArdaRHIQueueType Queue)
	{
		auto* Native = Cast<FArdaBuffer>(Buffer.Get());
		if (!Native || !Owns(Native))
		{
			return WrongDevice();
		}
		if (!Native->mDesc.mbTiled)
		{
			return Invalid("Tile mappings require a sparse buffer.");
		}
		if (!GetCapabilities().IsQueueSupported(Queue))
		{
			return Unsupported("The requested sparse-binding queue is unavailable.");
		}
		eastl::vector<FArdaProviderBufferTileMapping> Resolved;
		Resolved.reserve(Mappings.size());
		for (const auto& Mapping : Mappings)
		{
			FArdaProviderBufferTileMapping Entry;
			Entry.mBufferOffset = Mapping.mBufferOffset;
			Entry.mByteSize = Mapping.mByteSize;
			Entry.mHeapOffset = Mapping.mHeapOffset;
			Entry.mbCommit = Mapping.mbCommit;
			if (Mapping.mHeap)
			{
				auto* Heap = Cast<FArdaHeap>(Mapping.mHeap.Get());
				if (!Heap || !Owns(Heap))
				{
					return WrongDevice();
				}
				Entry.mHeap = Heap->mNative;
			}
			Resolved.push_back(eastl::move(Entry));
		}
		return mDevice->UpdateBufferTileMappings(Native->mNative, Resolved, Queue);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::CommitReservedResource(const FArdaRHIResourceRef& Resource,
	    uint64_t CommittedBytes,
	    EArdaRHIQueueType Queue)
	{
		if (!GetCapabilities().IsQueueSupported(Queue))
		{
			return Unsupported("The requested sparse-binding queue is unavailable.");
		}
		auto* Base = Cast<FArdaResource>(Resource.Get());
		if (!Base || !Owns(Base))
		{
			return WrongDevice();
		}
		if (auto* Texture = Cast<FArdaTexture>(Resource.Get()))
		{
			if (!Texture->mDesc.mbTiled)
			{
				return Invalid("Reserved commit requires a tiled texture.");
			}
			return mDevice->CommitReservedResource(Texture->mNative, true, CommittedBytes, Queue);
		}
		if (auto* Buffer = Cast<FArdaBuffer>(Resource.Get()))
		{
			if (!Buffer->mDesc.mbTiled)
			{
				return Invalid("Reserved commit requires a tiled buffer.");
			}
			return mDevice->CommitReservedResource(Buffer->mNative, false, CommittedBytes, Queue);
		}
		return Invalid("Reserved commit supports only tiled textures and buffers.");
	}

	TArdaRHIResult<FArdaRHIStreamingBudget> FArdaRHIDeviceImpl::QueryStreamingBudget(bool bLocalMemory) const
	{
		return mDevice->QueryStreamingBudget(bLocalMemory);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::SetStreamingBudgetReservation(uint64_t Bytes, bool bLocalMemory)
	{
		return mDevice->SetStreamingBudgetReservation(Bytes, bLocalMemory);
	}
}
