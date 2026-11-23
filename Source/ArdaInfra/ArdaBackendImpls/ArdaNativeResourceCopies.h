#pragma once

#include "NativeCompute/ArdaNativeCompute.h"
#include "RHI/Scheduling/ArdaRHICommandList.h"

namespace arda::native_provider_detail
{
	inline FArdaRHIStatus UnsupportedCopy(const char* Message)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Message);
	}

	inline FArdaRHIStatus SetViewState(IArdaRHICommandList& Commands,
	    const FArdaNativeResourceView& View,
	    EArdaRHIResourceState State)
	{
		if (auto* Buffer = dynamic_cast<IArdaRHIBuffer*>(View.mResource.Get()))
		{
			return Commands.SetBufferState(*Buffer, State);
		}
		if (auto* Texture = dynamic_cast<IArdaRHITexture*>(View.mResource.Get()))
		{
			return Commands.SetTextureState(*Texture, {View.mMipLevel, 1}, State);
		}
		return UnsupportedCopy("Native conversion requires a buffer or a texture view.");
	}

	inline FArdaRHITextureSlice TextureSlice(const FArdaNativeResourceView& View)
	{
		FArdaRHITextureSlice Slice;
		Slice.mMipLevel = View.mMipLevel;
		return Slice;
	}

	struct FArdaNativeResourceCopy
	{
		FArdaNativeResourceView mSource;
		FArdaNativeResourceView mDestination;
		EArdaRHIResourceState mSourceState;
		EArdaRHIResourceState mDestinationState;
		FArdaRHITextureBufferLayout mLayout;

		FArdaRHIStatus Copy(IArdaRHICommandList& Commands) const
		{
			auto* SourceBuffer = dynamic_cast<IArdaRHIBuffer*>(mSource.mResource.Get());
			auto* DestBuffer = dynamic_cast<IArdaRHIBuffer*>(mDestination.mResource.Get());
			if (SourceBuffer && DestBuffer)
			{
				const auto Range = mSource.mBufferRange.Resolve(SourceBuffer->GetDesc());
				return Commands.CopyBuffer(*DestBuffer,
				    mDestination.mBufferRange.mByteOffset,
				    *SourceBuffer,
				    Range.mByteOffset,
				    Range.mByteSize);
			}
			auto* SourceTexture = dynamic_cast<IArdaRHITexture*>(mSource.mResource.Get());
			auto* DestTexture = dynamic_cast<IArdaRHITexture*>(mDestination.mResource.Get());
			if (SourceBuffer && DestTexture)
			{
				return Commands.CopyBufferToTexture(*DestTexture, TextureSlice(mDestination), *SourceBuffer, mLayout);
			}
			if (SourceTexture && DestBuffer)
			{
				return Commands.CopyTextureToBuffer(*DestBuffer, mLayout, *SourceTexture, TextureSlice(mSource));
			}
			if (!SourceTexture || !DestTexture)
			{
				return UnsupportedCopy("Native conversion has an unsupported copy representation.");
			}
			auto SourceSlice = TextureSlice(mSource);
			auto DestSlice = TextureSlice(mDestination);
			for (uint32_t Layer = 0; Layer < SourceTexture->GetDesc().mArraySize; ++Layer)
			{
				SourceSlice.mArraySlice = DestSlice.mArraySlice = Layer;
				if (auto Status = Commands.CopyTexture(*DestTexture, DestSlice, *SourceTexture, SourceSlice); !Status)
				{
					return Status;
				}
			}
			return {};
		}

		FArdaRHIStatus Record(IArdaRHICommandList& Commands) const
		{
			if (auto Status = SetViewState(Commands, mSource, EArdaRHIResourceState::CopySource); !Status)
			{
				return Status;
			}
			if (auto Status = SetViewState(Commands, mDestination, EArdaRHIResourceState::CopyDest); !Status)
			{
				return Status;
			}
			Commands.CommitBarriers();
			if (auto Status = Copy(Commands); !Status)
			{
				return Status;
			}
			if (auto Status = SetViewState(Commands, mSource, mSourceState); !Status)
			{
				return Status;
			}
			if (auto Status = SetViewState(Commands, mDestination, mDestinationState); !Status)
			{
				return Status;
			}
			Commands.CommitBarriers();
			return {};
		}
	};

	inline TArdaRHIResult<FArdaNativeResourceView> CreateStagingBuffer(IArdaRHIDevice& Device,
	    const FArdaNativeResourceView& View,
	    EArdaRHIBufferUsage Usage,
	    bool bCudaInterop)
	{
		const auto* Source = dynamic_cast<const IArdaRHIBuffer*>(View.mResource.Get());
		if (!Source || Source->GetDesc().mCpuAccess != EArdaRHICpuAccess::None)
		{
			return {{}, UnsupportedCopy("Native staging needs a device-local graphics buffer.")};
		}
		const auto Range = View.mBufferRange.Resolve(Source->GetDesc());
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = Range.mByteSize;
		Desc.mUsage = Usage;
		Desc.mbCudaInterop = bCudaInterop;
		Desc.mDebugName = Source->GetDesc().mDebugName + ".native-stage";
		auto Buffer = Device.CreateBuffer(Desc);
		if (!Buffer)
		{
			return {{}, eastl::move(Buffer.mStatus)};
		}
		return {{eastl::move(Buffer.mValue), {0, Range.mByteSize}}, {}};
	}

	inline TArdaRHIResult<FArdaNativeResourceView> CreateStagingTexture(IArdaRHIDevice& Device,
	    const FArdaNativeResourceView& View,
	    const FArdaNativeResourceSlot& Slot,
	    EArdaRHITextureUsage Usage,
	    bool bCudaInterop)
	{
		const auto* Source = dynamic_cast<const IArdaRHITexture*>(View.mResource.Get());
		if (!Source)
		{
			return {{}, UnsupportedCopy("Native texture staging requires a graphics texture.")};
		}
		const auto& Original = Source->GetDesc();
		if ((Slot.mFormat != EArdaRHIFormat::Unknown && Original.mFormat != Slot.mFormat) ||
		    GetArdaRHIFormatInfo(Original.mFormat).mbDepth || GetArdaRHIFormatPlaneCount(Original.mFormat) != 1)
		{
			return {{}, UnsupportedCopy("Native staging copies one color plane in the original format.")};
		}
		FArdaRHITextureDesc Desc;
		Desc.mFormat = Original.mFormat;
		Desc.mWidth = GetArdaRHITextureMipExtent(Original.mWidth, View.mMipLevel);
		Desc.mHeight = GetArdaRHITextureMipExtent(Original.mHeight, View.mMipLevel);
		Desc.mDepth = GetArdaRHITextureMipExtent(Original.mDepth, View.mMipLevel);
		Desc.mDimension = Original.mDimension;
		Desc.mArraySize = Original.mArraySize;
		Desc.mSampleCount = Original.mSampleCount;
		Desc.mUsage = Usage;
		Desc.mbCudaInterop = bCudaInterop;
		Desc.mDebugName = Original.mDebugName + ".native-stage";
		FArdaRHITextureCopyExtent Extent;
		if (auto Status = ResolveArdaRHITextureCopyExtent(Desc, {}, Original, TextureSlice(View), Extent); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		auto Texture = Device.CreateTexture(Desc);
		if (!Texture)
		{
			return {{}, eastl::move(Texture.mStatus)};
		}
		return {{eastl::move(Texture.mValue), {}, 0}, {}};
	}

	inline TArdaRHIResult<FArdaRHITextureBufferLayout> GetCopyLayout(const FArdaNativeResourceView& Graphics,
	    const FArdaNativeResourceView& Native)
	{
		const auto* Texture = dynamic_cast<const IArdaRHITexture*>(Graphics.mResource.Get());
		const auto* Buffer = dynamic_cast<const IArdaRHIBuffer*>(Native.mResource.Get());
		if (!Texture || !Buffer)
		{
			return {{}, {}};
		}
		const auto Footprint = GetArdaRHITextureBufferFootprint(Texture->GetDesc(), TextureSlice(Graphics));
		if (!Footprint)
		{
			return {{}, Footprint.mStatus};
		}
		const auto& Layout = Footprint.mValue.mLayout;
		FArdaRHITextureCopyExtent Extent;
		if (auto Status = ValidateArdaRHITextureBufferCopy(Texture->GetDesc(),
		        TextureSlice(Graphics),
		        Buffer->GetDesc(),
		        Layout,
		        Extent);
		    !Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {Layout, {}};
	}

	inline bool ViewsOverlap(const FArdaNativeResourceView& A, const FArdaNativeResourceView& B)
	{
		if (A.mResource != B.mResource)
		{
			return false;
		}
		if (const auto* Buffer = dynamic_cast<const IArdaRHIBuffer*>(A.mResource.Get()))
		{
			const auto Left = A.mBufferRange.Resolve(Buffer->GetDesc());
			const auto Right = B.mBufferRange.Resolve(Buffer->GetDesc());
			return Left.mByteOffset < Right.mByteOffset + Right.mByteSize &&
			    Right.mByteOffset < Left.mByteOffset + Left.mByteSize;
		}
		return A.mMipLevel == B.mMipLevel;
	}

	using FArdaAllocateNativeStage = TArdaRHIResult<FArdaNativeResourceView> (*)(IArdaRHIDevice&,
	    const FArdaNativeResourceSlot&,
	    const FArdaNativeResourceView&);

	inline FArdaRHIStatus CheckConversionInputs(const FArdaNativeConversionContext& Context,
	    const FArdaNativeConversion& Conversion)
	{
		const size_t Count = Context.mGraphics.mResources.size();
		if (!Context.mContext.mDevice || Context.mNativeSlots.size() != Count ||
		    Conversion.mNative.mResources.size() != Count || Conversion.mGraphicsStates.size() != Count)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Native conversion requires a device, matching resource counts and graphics states.");
		}
		return {};
	}

	inline FArdaNativeWorkRef MakeViewTransition(FArdaNativeResourceView View, EArdaRHIResourceState State)
	{
		return MakeArdaNativeWork(
		    [View = eastl::move(View), State](IArdaRHICommandList& Commands)
		    {
			    auto Status = SetViewState(Commands, View, State);
			    if (Status)
			    {
				    Commands.CommitBarriers();
			    }
			    return Status;
		    });
	}

	inline FArdaRHIStatus StageGraphicsToNative(const FArdaNativeConversionContext& Context,
	    FArdaNativeConversion& Conversion,
	    FArdaAllocateNativeStage Allocate)
	{
		if (auto Status = CheckConversionInputs(Context, Conversion); !Status)
		{
			return Status;
		}
		const size_t Count = Context.mGraphics.mResources.size();
		for (size_t I = 0; I < Count; ++I)
		{
			const auto& Graphics = Context.mGraphics.mResources[I];
			const auto& Slot = Context.mNativeSlots[I];
			const auto GraphicsState = Conversion.mGraphicsStates[I];
			const auto NativeState = Context.mProvider.GetRequiredState(Slot);
			if (!Graphics.mResource || GraphicsState == EArdaRHIResourceState::Unknown ||
			    NativeState == EArdaRHIResourceState::Unknown)
			{
				return UnsupportedCopy("Native conversion requires a resource and graphics and native states.");
			}
			if (Graphics.mResource->GetResourceType() == Slot.mType)
			{
				auto Handoff = Context.mProvider.QueryHandoff(Context.mContext, Slot, Graphics);
				if (!Handoff)
				{
					return Handoff.mStatus;
				}
				if (Handoff.mValue.mKind == EArdaNativeHandoff::Direct)
				{
					if (GraphicsState != Handoff.mValue.mRequiredState)
					{
						Conversion.mBeforeNative.push_back(MakeViewTransition(Graphics, Handoff.mValue.mRequiredState));
					}
					continue;
				}
			}
			for (size_t Other = 0; Other < Count; ++Other)
			{
				if (I != Other && ViewsOverlap(Graphics, Context.mGraphics.mResources[Other]) &&
				    (Slot.mAccess != EArdaComputeAccess::Read ||
				        Context.mNativeSlots[Other].mAccess != EArdaComputeAccess::Read))
				{
					return UnsupportedCopy("Writable overlapping bindings need a shared native representation.");
				}
			}
			auto Native = Allocate(*Context.mContext.mDevice, Slot, Graphics);
			if (!Native)
			{
				return Native.mStatus;
			}
			Conversion.mNative.mResources[I] = Native.mValue;
			if (Slot.mAccess == EArdaComputeAccess::Write)
			{
				Conversion.mBeforeNative.push_back(MakeViewTransition(eastl::move(Native.mValue), NativeState));
				continue;
			}
			auto Layout = GetCopyLayout(Graphics, Native.mValue);
			if (!Layout)
			{
				return Layout.mStatus;
			}
			Conversion.mBeforeNative.push_back(MakeArdaNativeWork(
			    [Copy = FArdaNativeResourceCopy{Graphics,
			         eastl::move(Native.mValue),
			         GraphicsState,
			         NativeState,
			         Layout.mValue}](IArdaRHICommandList& Commands)
			    {
				    return Copy.Record(Commands);
			    }));
		}
		return {};
	}

	inline FArdaRHIStatus StageNativeToGraphics(const FArdaNativeConversionContext& Context,
	    FArdaNativeConversion& Conversion)
	{
		if (auto Status = CheckConversionInputs(Context, Conversion); !Status)
		{
			return Status;
		}
		for (size_t I = 0; I < Context.mNativeSlots.size(); ++I)
		{
			const auto& Slot = Context.mNativeSlots[I];
			const auto& Graphics = Context.mGraphics.mResources[I];
			const auto& Native = Conversion.mNative.mResources[I];
			const auto GraphicsState = Conversion.mGraphicsStates[I];
			const auto NativeState = Context.mProvider.GetRequiredState(Slot);
			if (Graphics.mResource == Native.mResource)
			{
				if (GraphicsState != NativeState)
				{
					Conversion.mAfterNative.push_back(MakeViewTransition(Graphics, GraphicsState));
				}
				continue;
			}
			if (Slot.mAccess == EArdaComputeAccess::Read)
			{
				continue;
			}
			const auto Layout = GetCopyLayout(Graphics, Native);
			if (!Layout)
			{
				return Layout.mStatus;
			}
			Conversion.mAfterNative.push_back(MakeArdaNativeWork(
			    [Copy = FArdaNativeResourceCopy{Native, Graphics, NativeState, GraphicsState, Layout.mValue}](
			        IArdaRHICommandList& Commands)
			    {
				    return Copy.Record(Commands);
			    }));
		}
		return {};
	}
}
