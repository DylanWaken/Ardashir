#include "ArdaDependencyGraphNodes.h"

namespace arda
{
	namespace
	{
		template <typename ParametersType>
		FArdaRHIStatus ResolveTransfer(FArdaDependencyResourceContext& Context, ParametersType& Parameters)
		{
			// Validate both logical resource kinds before deriving the exact native copy footprint.
			const auto* Texture = Context.Find(Parameters.mTexture);
			const auto* Buffer = Context.Find(Parameters.mBuffer);
			if (!Texture || !Buffer || !Texture->mbTexture || Buffer->mbTexture ||
			    Buffer->mExternalAccelerationStructure)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "A texture-buffer transfer requires resources of matching kinds from this graph.");
			}

			FArdaRHITextureCopyExtent Extent;
			if (auto Status = ValidateArdaRHITextureBufferCopy(Texture->mTexture,
			        Parameters.mSlice,
			        Buffer->mBuffer,
			        Parameters.mLayout,
			        Extent);
			    !Status)
			{
				return Status;
			}

			Parameters.mRowBytes = uint64_t(Extent.mWidth) * GetArdaRHIFormatElementSize(Texture->mTexture.mFormat);
			Parameters.mRowCount = uint64_t(Extent.mHeight) * Extent.mDepth;
			const auto& Slice = Parameters.mSlice;
			Parameters.mbWholeSubresource = !Slice.mX && !Slice.mY && !Slice.mZ &&
			    Extent.mWidth == GetArdaRHITextureMipExtent(Texture->mTexture.mWidth, Slice.mMipLevel) &&
			    Extent.mHeight == GetArdaRHITextureMipExtent(Texture->mTexture.mHeight, Slice.mMipLevel) &&
			    Extent.mDepth == GetArdaRHITextureMipExtent(Texture->mTexture.mDepth, Slice.mMipLevel);
			return {};
		}

		template <typename ParametersType>
		eastl::string TransferKey(const ParametersType& Parameters)
		{
			FArdaDependencyKeyBuilder Key;
			Key.Resource(Parameters.mTexture).Resource(Parameters.mBuffer);
			for (uint32_t Value : {Parameters.mSlice.mX,
			         Parameters.mSlice.mY,
			         Parameters.mSlice.mZ,
			         Parameters.mSlice.mWidth,
			         Parameters.mSlice.mHeight,
			         Parameters.mSlice.mDepth,
			         Parameters.mSlice.mMipLevel,
			         Parameters.mSlice.mArraySlice,
			         Parameters.mSlice.mPlane})
			{
				Key.Value(Value);
			}

			return Key.Value(Parameters.mLayout.mByteOffset)
			    .Value(Parameters.mLayout.mRowPitch)
			    .Value(Parameters.mRowBytes)
			    .Value(Parameters.mRowCount)
			    .Value(Parameters.mbWholeSubresource)
			    .Build();
		}

		template <typename ParametersType>
		FArdaDependencyNodeDesc DescribeTransfer(const ParametersType& Parameters, bool bToTexture)
		{
			// Texture hazards cover the copied subresource; buffer hazards exclude untouched row padding.
			FArdaDependencyNodeDesc Desc;
			FArdaDependencyAccess Texture{Parameters.mTexture,
			    bToTexture ? EArdaDependencyAccess::Write : EArdaDependencyAccess::Read,
			    bToTexture ? EArdaRHIResourceState::CopyDest : EArdaRHIResourceState::CopySource};
			Texture.mTextureRange =
			    {Parameters.mSlice.mMipLevel, 1, Parameters.mSlice.mArraySlice, 1, Parameters.mSlice.mPlane, 1};
			// Partial uploads preserve texels outside the rectangle, so they cannot initialize a fresh subresource.
			if (bToTexture && !Parameters.mbWholeSubresource)
			{
				Texture.mAccess = EArdaDependencyAccess::ReadWrite;
			}
			Desc.mAccesses.push_back(Texture);

			const bool bContiguous = Parameters.mRowBytes == Parameters.mLayout.mRowPitch;
			for (uint64_t Row = 0; Row < (bContiguous ? 1 : Parameters.mRowCount); ++Row)
			{
				FArdaDependencyAccess Buffer{Parameters.mBuffer,
				    bToTexture ? EArdaDependencyAccess::Read : EArdaDependencyAccess::Write,
				    bToTexture ? EArdaRHIResourceState::CopySource : EArdaRHIResourceState::CopyDest};
				Buffer.mBufferRange = {Parameters.mLayout.mByteOffset + Row * Parameters.mLayout.mRowPitch,
				    bContiguous ? Parameters.mRowCount * Parameters.mRowBytes : Parameters.mRowBytes};
				Desc.mAccesses.push_back(Buffer);
			}

			return Desc;
		}
	}

	FArdaRHIStatus InitializeArdaTextureTransferNodes(FArdaNodeRegistry& Registry)
	{
		if (auto Status = FArdaGraphTextureToBufferNode::Register(Registry); !Status)
		{
			return Status;
		}

		return FArdaGraphBufferToTextureNode::Register(Registry);
	}

	FArdaDependencyNodeMetadata FArdaGraphTextureToBufferNode::GetMetadata()
	{
		return {"arda.texture-to-buffer", 1};
	}

	FArdaRHIStatus FArdaGraphTextureToBufferNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		return ResolveTransfer(Context, Parameters);
	}

	eastl::string FArdaGraphTextureToBufferNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return TransferKey(Parameters);
	}

	FArdaDependencyNodeDesc FArdaGraphTextureToBufferNode::Describe(const FArdaParameters& Parameters,
	    const FArdaState&)
	{
		return DescribeTransfer(Parameters, false);
	}

	FArdaRHIStatus FArdaGraphTextureToBufferNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return Context.GetCommands().CopyTextureToBuffer(*Context.GetBuffer(Parameters.mBuffer),
		    Parameters.mLayout,
		    *Context.GetTexture(Parameters.mTexture),
		    Parameters.mSlice);
	}

	FArdaDependencyNodeMetadata FArdaGraphBufferToTextureNode::GetMetadata()
	{
		return {"arda.buffer-to-texture", 1};
	}

	FArdaRHIStatus FArdaGraphBufferToTextureNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		return ResolveTransfer(Context, Parameters);
	}

	eastl::string FArdaGraphBufferToTextureNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return TransferKey(Parameters);
	}

	FArdaDependencyNodeDesc FArdaGraphBufferToTextureNode::Describe(const FArdaParameters& Parameters,
	    const FArdaState&)
	{
		return DescribeTransfer(Parameters, true);
	}

	FArdaRHIStatus FArdaGraphBufferToTextureNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return Context.GetCommands().CopyBufferToTexture(*Context.GetTexture(Parameters.mTexture),
		    Parameters.mSlice,
		    *Context.GetBuffer(Parameters.mBuffer),
		    Parameters.mLayout);
	}
}
