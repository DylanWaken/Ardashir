#include "NodeLibrary/ArdaMemoryTextureNodes.h"

#include "ArdaDependencyKey.h"

#include <cstring>

namespace arda
{
	namespace
	{
		FArdaRHIStatus InvalidTexture(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		const FArdaRHITextureDesc* FindTexture(FArdaDependencyResourceContext& Context,
		    FArdaDependencyResourceHandle Resource)
		{
			const auto* Description = Context.Find(Resource);
			return Description && Description->mbTexture ? &Description->mTexture : nullptr;
		}

		void SetExtent(FArdaRHITextureSlice& Slice, const FArdaRHITextureCopyExtent& Extent)
		{
			Slice.mWidth = Extent.mWidth;
			Slice.mHeight = Extent.mHeight;
			Slice.mDepth = Extent.mDepth;
		}

		bool IsWholeSubresource(const FArdaRHITextureDesc& Description, const FArdaRHITextureSlice& Slice)
		{
			return !Slice.mX && !Slice.mY && !Slice.mZ &&
			    Slice.mWidth == GetArdaRHITextureMipExtent(Description.mWidth, Slice.mMipLevel) &&
			    Slice.mHeight == GetArdaRHITextureMipExtent(Description.mHeight, Slice.mMipLevel) &&
			    Slice.mDepth == GetArdaRHITextureMipExtent(Description.mDepth, Slice.mMipLevel);
		}

		void AddSliceKey(FArdaDependencyKeyBuilder& Key, const FArdaRHITextureSlice& Slice)
		{
			Key.Value(Slice.mX)
			    .Value(Slice.mY)
			    .Value(Slice.mZ)
			    .Value(Slice.mWidth)
			    .Value(Slice.mHeight)
			    .Value(Slice.mDepth)
			    .Value(Slice.mMipLevel)
			    .Value(Slice.mArraySlice)
			    .Value(Slice.mPlane);
		}

		FArdaDependencyAccess TextureAccess(FArdaDependencyResourceHandle Resource,
		    const FArdaRHITextureSlice& Slice,
		    EArdaDependencyAccess Access,
		    EArdaRHIResourceState State)
		{
			FArdaDependencyAccess Result{Resource, Access, State};
			Result.mTextureRange = {Slice.mMipLevel, 1, Slice.mArraySlice, 1, Slice.mPlane, 1};
			return Result;
		}

		bool IsExactExtent(const FArdaRHITextureSlice& Slice, const FArdaRHITextureCopyExtent& Extent)
		{
			return (Slice.mWidth == ArdaRHIAllSubresources || Slice.mWidth == Extent.mWidth) &&
			    (Slice.mHeight == ArdaRHIAllSubresources || Slice.mHeight == Extent.mHeight) &&
			    (Slice.mDepth == ArdaRHIAllSubresources || Slice.mDepth == Extent.mDepth);
		}

		bool ResolveCount(uint32_t Base, uint32_t& Count, uint32_t Total)
		{
			if (Base >= Total)
			{
				return false;
			}
			if (Count == ArdaRHIAllSubresources)
			{
				Count = Total - Base;
			}
			return Count && Count <= Total - Base;
		}

		uint32_t TextureDimensions(EArdaRHITextureDimension Dimension)
		{
			switch (Dimension)
			{
			case EArdaRHITextureDimension::Texture1D:
			case EArdaRHITextureDimension::Texture1DArray:
				return 1;
			case EArdaRHITextureDimension::Texture3D:
				return 3;
			case EArdaRHITextureDimension::Unknown:
				return 0;
			default:
				return 2;
			}
		}
	}

	FArdaDependencyNodeMetadata FArdaMemoryUploadTextureNode::GetMetadata()
	{
		return {"arda.memory.upload-texture", 1};
	}

	FArdaRHIStatus FArdaMemoryUploadTextureNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		const auto* Texture = FindTexture(Context, Parameters.mDestination);
		if (!Texture)
		{
			return InvalidTexture("Texture upload requires a destination texture from this graph.");
		}
		const auto Footprint = GetArdaRHITextureBufferFootprint(*Texture, Parameters.mSlice);
		if (!Footprint)
		{
			return Footprint.mStatus;
		}
		Parameters.mFootprint = Footprint.mValue;
		if (Parameters.mBytes.size() != Footprint.mValue.mRowBytes * Footprint.mValue.mRowCount)
		{
			return InvalidTexture("Texture upload bytes must exactly match the tightly packed region.");
		}
		SetExtent(Parameters.mSlice, Footprint.mValue.mExtent);
		Parameters.mbWholeSubresource = IsWholeSubresource(*Texture, Parameters.mSlice);
		return Context.Texture(Parameters.mDestination, "Destination", *Texture);
	}

	eastl::string FArdaMemoryUploadTextureNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(Parameters.mDestination).Bytes(Parameters.mBytes.data(), Parameters.mBytes.size());
		AddSliceKey(Key, Parameters.mSlice);
		return Key.Build();
	}

	FArdaDependencyNodeDesc FArdaMemoryUploadTextureNode::Describe(const FArdaParameters& Parameters, const FArdaState&)
	{
		FArdaDependencyNodeDesc Description;
		Description.mTransientWorkspaceBytes = Parameters.mFootprint.mByteSize;
		Description.mAccesses = {TextureAccess(Parameters.mDestination,
		    Parameters.mSlice,
		    Parameters.mbWholeSubresource ? EArdaDependencyAccess::Write : EArdaDependencyAccess::ReadWrite,
		    EArdaRHIResourceState::CopyDest)};
		return Description;
	}

	FArdaRHIStatus FArdaMemoryUploadTextureNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		const auto& Footprint = Parameters.mFootprint;
		const auto Buffer = Context.GetWorkspaceBuffer();
		if (!Buffer)
		{
			return InvalidTexture("Texture upload requires its declared transient workspace.");
		}
		eastl::vector<uint8_t> Pitched(Footprint.mByteSize, 0);
		for (uint64_t Row = 0; Row < Footprint.mRowCount; ++Row)
		{
			std::memcpy(Pitched.data() + Row * Footprint.mLayout.mRowPitch,
			    Parameters.mBytes.data() + Row * Footprint.mRowBytes,
			    Footprint.mRowBytes);
		}
		auto& Commands = Context.GetCommands();
		if (auto Status = Commands.SetBufferState(*Buffer, EArdaRHIResourceState::CopyDest); !Status)
		{
			return Status;
		}
		Commands.CommitBarriers();
		if (auto Status = Commands.WriteBuffer(*Buffer, Pitched.data(), Pitched.size()); !Status)
		{
			return Status;
		}
		if (auto Status = Commands.SetBufferState(*Buffer, EArdaRHIResourceState::CopySource); !Status)
		{
			return Status;
		}
		Commands.CommitBarriers();
		if (auto Status = Commands.CopyBufferToTexture(*Context.GetTexture(Parameters.mDestination),
		        Parameters.mSlice,
		        *Buffer,
		        Footprint.mLayout);
		    !Status)
		{
			return Status;
		}
		if (auto Status = Commands.SetBufferState(*Buffer, EArdaRHIResourceState::UnorderedAccess); !Status)
		{
			return Status;
		}
		Commands.CommitBarriers();
		return {};
	}

	FArdaDependencyNodeMetadata FArdaMemoryReadbackTextureNode::GetMetadata()
	{
		return {"arda.memory.readback-texture", 1};
	}

	FArdaRHIStatus FArdaMemoryReadbackTextureNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		const auto* Texture = FindTexture(Context, Parameters.mSource);
		if (!Texture || !Parameters.mDestination)
		{
			return InvalidTexture("Texture readback requires a source texture and retained byte destination.");
		}
		const auto Footprint = GetArdaRHITextureBufferFootprint(*Texture, Parameters.mSlice);
		if (!Footprint)
		{
			return Footprint.mStatus;
		}
		SetExtent(Parameters.mSlice, Footprint.mValue.mExtent);
		Parameters.mWorkspaceBytes = Footprint.mValue.mByteSize;
		return {};
	}

	eastl::string FArdaMemoryReadbackTextureNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(Parameters.mSource).Value(reinterpret_cast<uintptr_t>(Parameters.mDestination.get()));
		AddSliceKey(Key, Parameters.mSlice);
		return Key.Build();
	}

	FArdaDependencyNodeDesc FArdaMemoryReadbackTextureNode::Describe(const FArdaParameters& Parameters,
	    const FArdaState&)
	{
		FArdaDependencyNodeDesc Description;
		Description.mTransientWorkspaceBytes = Parameters.mWorkspaceBytes;
		Description.mbSideEffect = true;
		Description.mAccesses = {TextureAccess(Parameters.mSource,
		    Parameters.mSlice,
		    EArdaDependencyAccess::Read,
		    EArdaRHIResourceState::CopySource)};
		return Description;
	}

	FArdaRHIStatus FArdaMemoryReadbackTextureNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return Context.ReadbackTexture(Parameters.mSource, Parameters.mDestination, Parameters.mSlice);
	}

	FArdaDependencyNodeMetadata FArdaMemoryCopyTextureNode::GetMetadata()
	{
		return {"arda.memory.copy-texture", 1};
	}

	FArdaRHIStatus FArdaMemoryCopyTextureNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		const auto* Source = FindTexture(Context, Parameters.mSource);
		const auto* Destination = FindTexture(Context, Parameters.mDestination);
		if (!Source || !Destination)
		{
			return InvalidTexture("Texture copy requires two valid textures from this graph.");
		}
		const auto& Format = GetArdaRHIFormatInfo(Source->mFormat);
		if (TextureDimensions(Source->mDimension) != TextureDimensions(Destination->mDimension))
		{
			return InvalidTexture("Texture copies require matching native texture dimensions (1D, 2D, or 3D).");
		}
		if (Source->mSampleCount != 1 || Destination->mSampleCount != 1 || Format.mbDepth || Format.mbStencil ||
		    HasAnyFlags(Source->mUsage | Destination->mUsage, EArdaRHITextureUsage::Typeless))
		{
			return InvalidTexture("Memory texture copies require typed, single-sample color textures.");
		}
		if (Parameters.mSource == Parameters.mDestination &&
		    Parameters.mSourceSlice.mMipLevel == Parameters.mDestinationSlice.mMipLevel &&
		    Parameters.mSourceSlice.mArraySlice == Parameters.mDestinationSlice.mArraySlice &&
		    Parameters.mSourceSlice.mPlane == Parameters.mDestinationSlice.mPlane)
		{
			return InvalidTexture("A texture copy cannot target its own source subresource.");
		}
		FArdaRHITextureCopyExtent Extent;
		if (auto Status = ResolveArdaRHITextureCopyExtent(*Destination,
		        Parameters.mDestinationSlice,
		        *Source,
		        Parameters.mSourceSlice,
		        Extent);
		    !Status)
		{
			return Status;
		}
		if (!IsExactExtent(Parameters.mSourceSlice, Extent) || !IsExactExtent(Parameters.mDestinationSlice, Extent))
		{
			return InvalidTexture("Explicit texture copy extents must match and fit without clipping.");
		}
		SetExtent(Parameters.mSourceSlice, Extent);
		SetExtent(Parameters.mDestinationSlice, Extent);
		Parameters.mbWholeSubresource = IsWholeSubresource(*Destination, Parameters.mDestinationSlice);
		return Context.Texture(Parameters.mDestination, "Destination", *Destination);
	}

	eastl::string FArdaMemoryCopyTextureNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(Parameters.mSource).Resource(Parameters.mDestination);
		AddSliceKey(Key, Parameters.mSourceSlice);
		AddSliceKey(Key, Parameters.mDestinationSlice);
		return Key.Build();
	}

	FArdaDependencyNodeDesc FArdaMemoryCopyTextureNode::Describe(const FArdaParameters& Parameters, const FArdaState&)
	{
		FArdaDependencyNodeDesc Description;
		Description.mAccesses = {TextureAccess(Parameters.mSource,
		                             Parameters.mSourceSlice,
		                             EArdaDependencyAccess::Read,
		                             EArdaRHIResourceState::CopySource),
		    TextureAccess(Parameters.mDestination,
		        Parameters.mDestinationSlice,
		        Parameters.mbWholeSubresource ? EArdaDependencyAccess::Write : EArdaDependencyAccess::ReadWrite,
		        EArdaRHIResourceState::CopyDest)};
		return Description;
	}

	FArdaRHIStatus FArdaMemoryCopyTextureNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return Context.GetCommands().CopyTexture(*Context.GetTexture(Parameters.mDestination),
		    Parameters.mDestinationSlice,
		    *Context.GetTexture(Parameters.mSource),
		    Parameters.mSourceSlice);
	}

	FArdaDependencyNodeMetadata FArdaMemoryClearTextureNode::GetMetadata()
	{
		return {"arda.memory.clear-texture", 1};
	}

	FArdaRHIStatus FArdaMemoryClearTextureNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		const auto* Destination = FindTexture(Context, Parameters.mDestination);
		if (!Destination)
		{
			return InvalidTexture("Texture clear requires a destination texture from this graph.");
		}
		const auto& Format = GetArdaRHIFormatInfo(Destination->mFormat);
		if (Format.mbDepth || Format.mbStencil || Format.mbInteger || Format.mBlockWidth != 1 ||
		    !HasAnyFlags(Destination->mUsage, EArdaRHITextureUsage::RenderTarget) ||
		    HasAnyFlags(Destination->mUsage, EArdaRHITextureUsage::Typeless))
		{
			return InvalidTexture("Color clear requires a typed noninteger color render-target texture.");
		}
		auto& Range = Parameters.mRange;
		if (!ResolveCount(Range.mBaseMipLevel, Range.mMipLevelCount, Destination->mMipLevels) ||
		    !ResolveCount(Range.mBaseArraySlice, Range.mArraySliceCount, Destination->mArraySize) ||
		    !ResolveCount(Range.mBasePlane, Range.mPlaneCount, 1))
		{
			return InvalidTexture("Texture clear subresource range is empty or out of bounds.");
		}
		return Context.Texture(Parameters.mDestination, "Destination", *Destination);
	}

	eastl::string FArdaMemoryClearTextureNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		const auto& Range = Parameters.mRange;
		const auto& Color = Parameters.mColor;
		return FArdaDependencyKeyBuilder()
		    .Resource(Parameters.mDestination)
		    .Value(Range.mBaseMipLevel)
		    .Value(Range.mMipLevelCount)
		    .Value(Range.mBaseArraySlice)
		    .Value(Range.mArraySliceCount)
		    .Value(Range.mBasePlane)
		    .Value(Range.mPlaneCount)
		    .Value(Color.mR)
		    .Value(Color.mG)
		    .Value(Color.mB)
		    .Value(Color.mA)
		    .Build();
	}

	FArdaDependencyNodeDesc FArdaMemoryClearTextureNode::Describe(const FArdaParameters& Parameters, const FArdaState&)
	{
		FArdaDependencyNodeDesc Description;
		FArdaDependencyAccess Access{Parameters.mDestination,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::RenderTarget};
		Access.mTextureRange = Parameters.mRange;
		Description.mAccesses = {Access};
		return Description;
	}

	FArdaRHIStatus FArdaMemoryClearTextureNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return Context.GetCommands().ClearTexture(*Context.GetTexture(Parameters.mDestination),
		    Parameters.mRange,
		    Parameters.mColor);
	}

	FArdaRHIStatus RegisterArdaMemoryTextureNodes()
	{
		if (auto Status = FArdaMemoryUploadTextureNode::Register(); !Status)
		{
			return Status;
		}
		if (auto Status = FArdaMemoryReadbackTextureNode::Register(); !Status)
		{
			return Status;
		}
		if (auto Status = FArdaMemoryCopyTextureNode::Register(); !Status)
		{
			return Status;
		}
		return FArdaMemoryClearTextureNode::Register();
	}
}
