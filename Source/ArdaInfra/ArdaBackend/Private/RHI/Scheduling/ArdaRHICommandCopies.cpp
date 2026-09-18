#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	FArdaRHIStatus FArdaCommandList::WriteBuffer(IArdaRHIBuffer& Buffer,
	    const void* Data,
	    size_t Size,
	    uint64_t Offset)
	{
		auto* Native = Cast<FArdaBuffer>(&Buffer);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		if (!Data || Size == 0 || Offset > Native->mDesc.mByteSize || Size > Native->mDesc.mByteSize - Offset)
		{
			return Invalid("Buffer write range is invalid.");
		}
		return mNative->WriteBuffer(Native->mNative, Native->mDesc, Data, Size, Offset);
	}

	FArdaRHIStatus FArdaCommandList::CopyBufferHostToDevice(IArdaRHIBuffer& Destination,
	    const void* SourceData,
	    size_t Size,
	    uint64_t DestinationOffset)
	{
		if (auto Status = WriteBuffer(Destination, SourceData, Size, DestinationOffset); !Status)
		{
			return Status;
		}
		FArdaPendingBufferCopyCompletion Completion;
		Completion.mbBlocking = true;
		mCopyCompletions.push_back(eastl::move(Completion));
		return {};
	}

	FArdaRHIStatus FArdaCommandList::CopyBufferHostToDeviceAsync(IArdaRHIBuffer& Destination,
	    const void* SourceData,
	    size_t Size,
	    FArdaRHIHostToDeviceCopyCallback Callback,
	    uint64_t DestinationOffset)
	{
		if (!Callback)
		{
			return Invalid("An asynchronous host-to-device copy requires a callback.");
		}
		if (auto Status = WriteBuffer(Destination, SourceData, Size, DestinationOffset); !Status)
		{
			return Status;
		}
		FArdaPendingBufferCopyCompletion Completion;
		Completion.mUploadCallback = eastl::move(Callback);
		mCopyCompletions.push_back(eastl::move(Completion));
		return {};
	}

	FArdaRHIStatus FArdaCommandList::QueueBufferReadback(IArdaRHIBuffer& Source,
	    uint64_t SourceOffset,
	    uint64_t Size,
	    FArdaPendingBufferCopyCompletion Completion)
	{
		auto* Native = Cast<FArdaBuffer>(&Source);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		if (SourceOffset > Native->mDesc.mByteSize)
		{
			return Invalid("Buffer readback offset is invalid.");
		}
		const uint64_t ResolvedSize = Size == ArdaRHIWholeBuffer ? Native->mDesc.mByteSize - SourceOffset : Size;
		if (ResolvedSize == 0 || ResolvedSize > Native->mDesc.mByteSize - SourceOffset ||
		    ResolvedSize > static_cast<uint64_t>(SIZE_MAX))
		{
			return Invalid("Buffer readback range is invalid.");
		}

		FArdaRHIBufferDesc ReadbackDesc;
		ReadbackDesc.mByteSize = ResolvedSize;
		ReadbackDesc.mCpuAccess = EArdaRHICpuAccess::Read;
		ReadbackDesc.mInitialState = EArdaRHIResourceState::CopyDest;
		ReadbackDesc.mbKeepInitialState = true;
		ReadbackDesc.mDebugName = "Buffer readback";
		auto Readback = mDevice->GetProviderDevice().AllocateBuffer(ReadbackDesc, mQueue);
		if (!Readback)
		{
			return eastl::move(Readback.mStatus);
		}
		if (auto Status = SetBufferState(Source, EArdaRHIResourceState::CopySource); !Status)
		{
			return Status;
		}
		if (auto Status = mNative->CopyBuffer(Readback.mValue, 0, Native->mNative, SourceOffset, ResolvedSize);
		    !Status)
		{
			return Status;
		}

		Completion.mReadbackBuffer = eastl::move(Readback.mValue);
		Completion.mByteSize = static_cast<size_t>(ResolvedSize);
		mCopyCompletions.push_back(eastl::move(Completion));
		return {};
	}

	FArdaRHIStatus FArdaCommandList::CopyBufferDeviceToHost(IArdaRHIBuffer& Source,
	    eastl::vector<uint8_t>& Output,
	    uint64_t SourceOffset,
	    uint64_t Size)
	{
		FArdaPendingBufferCopyCompletion Completion;
		Completion.mbBlocking = true;
		Completion.mOutput = &Output;
		return QueueBufferReadback(Source, SourceOffset, Size, eastl::move(Completion));
	}

	FArdaRHIStatus FArdaCommandList::CopyBufferDeviceToHostAsync(IArdaRHIBuffer& Source,
	    FArdaRHIDeviceToHostCopyCallback Callback,
	    uint64_t SourceOffset,
	    uint64_t Size)
	{
		if (!Callback)
		{
			return Invalid("An asynchronous device-to-host copy requires a callback.");
		}
		FArdaPendingBufferCopyCompletion Completion;
		Completion.mReadbackCallback = eastl::move(Callback);
		return QueueBufferReadback(Source, SourceOffset, Size, eastl::move(Completion));
	}

	FArdaRHIStatus FArdaCommandList::CopyBuffer(IArdaRHIBuffer& Destination,
	    uint64_t DestinationOffset,
	    IArdaRHIBuffer& Source,
	    uint64_t SourceOffset,
	    uint64_t Size)
	{
		auto* Dst = Cast<FArdaBuffer>(&Destination);
		auto* Src = Cast<FArdaBuffer>(&Source);
		if (!Dst || !Src || !RetainOwned(Dst) || !RetainOwned(Src))
		{
			return WrongDevice();
		}
		if (Size == 0 || DestinationOffset > Dst->mDesc.mByteSize ||
		    Size > Dst->mDesc.mByteSize - DestinationOffset || SourceOffset > Src->mDesc.mByteSize ||
		    Size > Src->mDesc.mByteSize - SourceOffset)
		{
			return Invalid("Buffer copy range is invalid.");
		}
		return mNative->CopyBuffer(Dst->mNative, DestinationOffset, Src->mNative, SourceOffset, Size);
	}

	FArdaRHIStatus FArdaCommandList::CopyTexture(IArdaRHITexture& Destination,
	    const FArdaRHITextureSlice& DestinationSlice,
	    IArdaRHITexture& Source,
	    const FArdaRHITextureSlice& SourceSlice)
	{
		auto* Dst = Cast<FArdaTexture>(&Destination);
		auto* Src = Cast<FArdaTexture>(&Source);
		if (!Dst || !Src || !RetainOwned(Dst) || !RetainOwned(Src))
		{
			return WrongDevice();
		}
		FArdaRHITextureCopyExtent Extent;
		if (auto Status =
		        ResolveArdaRHITextureCopyExtent(Dst->mDesc, DestinationSlice, Src->mDesc, SourceSlice, Extent);
		    !Status)
		{
			return Status;
		}
		if (Dst == Src && DestinationSlice.mMipLevel == SourceSlice.mMipLevel &&
		    DestinationSlice.mArraySlice == SourceSlice.mArraySlice &&
		    DestinationSlice.mPlane == SourceSlice.mPlane)
		{
			return Invalid("A texture subresource cannot be copied onto itself.");
		}
		return mNative
		    ->CopyTexture(Dst->mNative, Dst->mDesc, DestinationSlice, Src->mNative, Src->mDesc, SourceSlice);
	}

	FArdaRHIStatus FArdaCommandList::CopyBufferToTexture(IArdaRHITexture& Destination,
	    const FArdaRHITextureSlice& DestinationSlice,
	    IArdaRHIBuffer& Source,
	    const FArdaRHITextureBufferLayout& SourceLayout)
	{
		if (!mNative->IsOpen())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Texture-buffer copies require an open command list.");
		}
		auto* Dst = Cast<FArdaTexture>(&Destination);
		auto* Src = Cast<FArdaBuffer>(&Source);
		if (!Dst || !Src || !RetainOwned(Dst) || !RetainOwned(Src))
		{
			return WrongDevice();
		}
		if (Src->mDesc.mCpuAccess == EArdaRHICpuAccess::Read)
		{
			return Invalid("A CPU-read buffer cannot be the source of a texture copy.");
		}
		FArdaRHITextureCopyExtent Extent;
		if (auto Status =
		        ValidateArdaRHITextureBufferCopy(Dst->mDesc, DestinationSlice, Src->mDesc, SourceLayout, Extent);
		    !Status)
		{
			return Status;
		}
		return mNative->CopyBufferToTexture(Dst->mNative,
		    Dst->mDesc,
		    DestinationSlice,
		    Src->mNative,
		    Src->mDesc,
		    SourceLayout);
	}

	FArdaRHIStatus FArdaCommandList::CopyTextureToBuffer(IArdaRHIBuffer& Destination,
	    const FArdaRHITextureBufferLayout& DestinationLayout,
	    IArdaRHITexture& Source,
	    const FArdaRHITextureSlice& SourceSlice)
	{
		if (!mNative->IsOpen())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Texture-buffer copies require an open command list.");
		}
		auto* Dst = Cast<FArdaBuffer>(&Destination);
		auto* Src = Cast<FArdaTexture>(&Source);
		if (!Dst || !Src || !RetainOwned(Dst) || !RetainOwned(Src))
		{
			return WrongDevice();
		}
		if (Dst->mDesc.mCpuAccess == EArdaRHICpuAccess::Write)
		{
			return Invalid("A CPU-write buffer cannot be the destination of a texture copy.");
		}
		FArdaRHITextureCopyExtent Extent;
		if (auto Status =
		        ValidateArdaRHITextureBufferCopy(Src->mDesc, SourceSlice, Dst->mDesc, DestinationLayout, Extent);
		    !Status)
		{
			return Status;
		}
		return mNative->CopyTextureToBuffer(Dst->mNative,
		    Dst->mDesc,
		    DestinationLayout,
		    Src->mNative,
		    Src->mDesc,
		    SourceSlice);
	}

	FArdaRHIStatus FArdaCommandList::ResolveTexture(IArdaRHITexture& Destination,
	    const FArdaRHITextureSlice& DestinationSlice,
	    IArdaRHITexture& Source,
	    const FArdaRHITextureSlice& SourceSlice)
	{
		auto* Dst = Cast<FArdaTexture>(&Destination);
		auto* Src = Cast<FArdaTexture>(&Source);
		if (!Dst || !Src || !RetainOwned(Dst) || !RetainOwned(Src))
		{
			return WrongDevice();
		}
		FArdaRHITextureCopyExtent Extent;
		if (auto Status =
		        ValidateArdaRHITextureResolve(Dst->mDesc, DestinationSlice, Src->mDesc, SourceSlice, Extent);
		    !Status)
		{
			return Status;
		}
		return mNative
		    ->ResolveTexture(Dst->mNative, Dst->mDesc, DestinationSlice, Src->mNative, Src->mDesc, SourceSlice);
	}

	FArdaRHIStatus FArdaCommandList::CopyTextureToStaging(IArdaRHIStagingTexture& Destination,
	    const FArdaRHITextureSlice& DestinationSlice,
	    IArdaRHITexture& Source,
	    const FArdaRHITextureSlice& SourceSlice)
	{
		auto* Dst = Cast<FArdaStagingTexture>(&Destination);
		auto* Src = Cast<FArdaTexture>(&Source);
		if (!Dst || !Src || !RetainOwned(Dst) || !RetainOwned(Src))
		{
			return WrongDevice();
		}
		if (Dst->mDesc.mCpuAccess != EArdaRHICpuAccess::Read)
		{
			return Invalid("A texture readback requires a read staging texture.");
		}
		FArdaRHITextureCopyExtent Extent;
		if (auto Status = ResolveArdaRHITextureCopyExtent(Dst->mDesc.mTexture,
		        DestinationSlice,
		        Src->mDesc,
		        SourceSlice,
		        Extent);
		    !Status)
		{
			return Status;
		}
		return mNative->CopyTextureToStaging(Dst->mNative,
		    Dst->mDesc,
		    DestinationSlice,
		    Src->mNative,
		    Src->mDesc,
		    SourceSlice);
	}

	FArdaRHIStatus FArdaCommandList::CopyTextureFromStaging(IArdaRHITexture& Destination,
	    const FArdaRHITextureSlice& DestinationSlice,
	    IArdaRHIStagingTexture& Source,
	    const FArdaRHITextureSlice& SourceSlice)
	{
		auto* Dst = Cast<FArdaTexture>(&Destination);
		auto* Src = Cast<FArdaStagingTexture>(&Source);
		if (!Dst || !Src || !RetainOwned(Dst) || !RetainOwned(Src))
		{
			return WrongDevice();
		}
		if (Src->mDesc.mCpuAccess != EArdaRHICpuAccess::Write)
		{
			return Invalid("A texture upload requires a write staging texture.");
		}
		FArdaRHITextureCopyExtent Extent;
		if (auto Status = ResolveArdaRHITextureCopyExtent(Dst->mDesc,
		        DestinationSlice,
		        Src->mDesc.mTexture,
		        SourceSlice,
		        Extent);
		    !Status)
		{
			return Status;
		}
		return mNative->CopyTextureFromStaging(Dst->mNative,
		    Dst->mDesc,
		    DestinationSlice,
		    Src->mNative,
		    Src->mDesc,
		    SourceSlice);
	}

	FArdaRHIStatus FArdaCommandList::ClearTexture(IArdaRHITexture& Texture,
	    const FArdaRHITextureSubresourceRange& Range,
	    const FArdaRHIColor& Color)
	{
		auto* Native = Cast<FArdaTexture>(&Texture);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		return mNative->ClearTexture(Native->mNative, Native->mDesc, Range, Color);
	}

	FArdaRHIStatus FArdaCommandList::ClearTextureUInt(IArdaRHITexture& Texture,
	    const FArdaRHITextureSubresourceRange& Range,
	    uint32_t Value)
	{
		auto* Native = Cast<FArdaTexture>(&Texture);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		if (!GetArdaRHIFormatInfo(Native->mDesc.mFormat).mbInteger || Native->mDesc.mSampleCount != 1 ||
		    !HasAnyFlags(Native->mDesc.mUsage, EArdaRHITextureUsage::UnorderedAccess))
		{
			return Invalid("Integer texture clears require a single-sample integer UAV texture.");
		}
		const auto Resolved = Range.Resolve(Native->mDesc);
		if (!Resolved.mMipLevelCount || !Resolved.mArraySliceCount || !Resolved.mPlaneCount)
		{
			return Invalid("The integer texture clear range is empty.");
		}
		return mNative->ClearTextureUInt(Native->mNative, Native->mDesc, Range, Value);
	}

	FArdaRHIStatus FArdaCommandList::ClearDepthStencilTexture(IArdaRHITexture& Texture,
	    const FArdaRHITextureSubresourceRange& Range,
	    bool bClearDepth,
	    float Depth,
	    bool bClearStencil,
	    uint8_t Stencil)
	{
		auto* Native = Cast<FArdaTexture>(&Texture);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		return mNative->ClearDepthStencilTexture(Native->mNative,
		    Native->mDesc,
		    Range,
		    bClearDepth,
		    Depth,
		    bClearStencil,
		    Stencil);
	}

	FArdaRHIStatus FArdaCommandList::ClearBufferUInt(IArdaRHIBuffer& Buffer, uint32_t Value)
	{
		auto* Native = Cast<FArdaBuffer>(&Buffer);
		if (!Native || !RetainOwned(Native))
		{
			return WrongDevice();
		}
		if (Native->mDesc.mByteSize % sizeof(uint32_t) != 0 ||
		    !HasAnyFlags(Native->mDesc.mUsage, EArdaRHIBufferUsage::UnorderedAccess))
		{
			return Invalid("Integer buffer clears require a UAV buffer with a multiple-of-four byte size.");
		}
		return mNative->ClearBufferUInt(Native->mNative, Native->mDesc, Value);
	}

	FArdaRHIStatus FArdaCommandList::ClearSamplerFeedbackTexture(IArdaRHISamplerFeedbackTexture& Resource)
	{
		auto* Feedback = Cast<FArdaSamplerFeedbackTexture>(&Resource);
		if (!Feedback || !RetainOwned(Feedback))
		{
			return WrongDevice();
		}
		if (mDevice->GetCapabilities().mSamplerFeedbackTier == EArdaRHISamplerFeedbackTier::None)
		{
			return Unsupported("Sampler feedback is unsupported by this device.");
		}
		const FArdaRHIStatus Status = mNative->ClearSamplerFeedbackTexture(Feedback->mNative);
		if (Status)
		{
			mFacadeSamplerFeedbackStates[Feedback] = EArdaRHIResourceState::UnorderedAccess;
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::DecodeSamplerFeedbackTexture(IArdaRHITexture& DestinationResource,
	    IArdaRHISamplerFeedbackTexture& FeedbackResource,
	    EArdaRHIFormat Format)
	{
		auto* Destination = Cast<FArdaTexture>(&DestinationResource);
		auto* Feedback = Cast<FArdaSamplerFeedbackTexture>(&FeedbackResource);
		if (!Destination || !Feedback || !RetainOwned(Destination) || !RetainOwned(Feedback))
		{
			return WrongDevice();
		}
		if (Format != EArdaRHIFormat::R8UInt || Destination->mDesc.mFormat != Format)
		{
			return Invalid("Decoded sampler feedback requires an R8UInt destination texture.");
		}
		const auto& Paired = Feedback->mPairedTexture->GetDesc();
		const auto& Desc = Destination->mDesc;
		const auto& Region = Feedback->mDesc;
		const uint32_t MipCount =
		    Region.mFormat == EArdaRHISamplerFeedbackFormat::MinMipOpaque ? 1u : Paired.mMipLevels;
		if (Desc.mDimension != EArdaRHITextureDimension::Texture2D || Desc.mSampleCount != 1 ||
		    Desc.mArraySize != Paired.mArraySize || Desc.mMipLevels != MipCount ||
		    Desc.mWidth < (uint64_t(Paired.mWidth) + Region.mMipRegionX - 1) / Region.mMipRegionX ||
		    Desc.mHeight < (uint64_t(Paired.mHeight) + Region.mMipRegionY - 1) / Region.mMipRegionY)
		{
			return Invalid("The decoded texture must contain every feedback region, mip and array slice.");
		}
		const FArdaRHIStatus Status = mNative->DecodeSamplerFeedbackTexture(Destination->mNative,
		    Destination->mDesc,
		    Feedback->mNative,
		    Format);
		if (Status)
		{
			mFacadeSamplerFeedbackStates[Feedback] = EArdaRHIResourceState::ResolveSource;
			StoreTextureState(*Destination, {}, EArdaRHIResourceState::ResolveDest);
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::SetSamplerFeedbackTextureState(IArdaRHISamplerFeedbackTexture& Resource,
	    EArdaRHIResourceState State)
	{
		auto* Feedback = Cast<FArdaSamplerFeedbackTexture>(&Resource);
		if (!Feedback || !RetainOwned(Feedback))
		{
			return WrongDevice();
		}
		const FArdaRHIStatus Status = mNative->SetSamplerFeedbackTextureState(Feedback->mNative, State);
		if (Status)
		{
			mFacadeSamplerFeedbackStates[Feedback] = State;
		}
		return Status;
	}
}
