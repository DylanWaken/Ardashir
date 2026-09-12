#include "ArdaDependencyGraphNodes.h"

namespace arda
{
	namespace
	{
		struct FTextureTransferParameters
		{
			FArdaDependencyResourceHandle mTexture, mBuffer;
			FArdaRHITextureSlice mSlice;
			FArdaRHITextureBufferLayout mLayout;
			uint64_t mRowBytes = 0, mRowCount = 0;
		};

		template <class T>
		void Append(eastl::string& Key, T Value)
		{
			Key.append(reinterpret_cast<const char*>(&Value), sizeof(Value));
		}

		TArdaRHIResult<FArdaGraphNodeHandle> AttachTransfer(FArdaDependencyGraph& Graph,
		    eastl::string Name,
		    FArdaDependencyResourceHandle Texture,
		    const FArdaRHITextureSlice& Slice,
		    FArdaDependencyResourceHandle Buffer,
		    const FArdaRHITextureBufferLayout& Layout,
		    bool ToTexture)
		{
			const auto* T = Graph.FindResource(Texture);
			const auto* B = Graph.FindResource(Buffer);
			if (!T || !B || !T->mbTexture || B->mbTexture || B->mExternalAccelerationStructure || Name.empty())
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				        "A texture-buffer transfer requires a name and resources of matching kinds from this graph.")};
			}
			FArdaRHITextureCopyExtent Extent;
			if (auto S = ValidateArdaRHITextureBufferCopy(T->mTexture, Slice, B->mBuffer, Layout, Extent); !S)
			{
				return {{}, S};
			}
			FTextureTransferParameters P{Texture,
			    Buffer,
			    Slice,
			    Layout,
			    uint64_t(Extent.mWidth) * GetArdaRHIFormatElementSize(T->mTexture.mFormat),
			    uint64_t(Extent.mHeight) * Extent.mDepth};
			return Graph.AttachOrFind(eastl::move(Name),
			    ToTexture ? "arda.buffer-to-texture" : "arda.texture-to-buffer",
			    P);
		}
	}

	FArdaRHIStatus InitializeArdaTextureTransferNodes(FArdaNodeRegistry& Registry)
	{
		for (bool ToTexture : {false, true})
		{
			TArdaDependencyNodeDefinition<FTextureTransferParameters> D;
			D.mName = ToTexture ? "arda.buffer-to-texture" : "arda.texture-to-buffer";
			D.mKind = EArdaDependencyNodeKind::Graphics;
			D.mCanonicalKey = [](const FTextureTransferParameters& P)
			{
				eastl::string K;
				for (auto H : {P.mTexture, P.mBuffer})
				{
					Append(K, H.mGraph);
					Append(K, H.mIndex);
					Append(K, H.mGeneration);
				}
				for (uint32_t V : {P.mSlice.mX,
				         P.mSlice.mY,
				         P.mSlice.mZ,
				         P.mSlice.mWidth,
				         P.mSlice.mHeight,
				         P.mSlice.mDepth,
				         P.mSlice.mMipLevel,
				         P.mSlice.mArraySlice,
				         P.mSlice.mPlane})
				{
					Append(K, V);
				}
				Append(K, P.mLayout.mByteOffset);
				Append(K, P.mLayout.mRowPitch);
				Append(K, P.mRowBytes);
				Append(K, P.mRowCount);
				return K;
			};
			D.mDescribe = [ToTexture](const FTextureTransferParameters& P)
			{
				FArdaDependencyNodeDesc N;
				FArdaDependencyAccess T{P.mTexture,
				    ToTexture ? EArdaDependencyAccess::Write : EArdaDependencyAccess::Read,
				    ToTexture ? EArdaRHIResourceState::CopyDest : EArdaRHIResourceState::CopySource};
				T.mTextureRange = {P.mSlice.mMipLevel, 1, P.mSlice.mArraySlice, 1, P.mSlice.mPlane, 1};
				N.mAccesses.push_back(T);
				const bool Contiguous = P.mRowBytes == P.mLayout.mRowPitch;
				for (uint64_t Row = 0; Row < (Contiguous ? 1 : P.mRowCount); ++Row)
				{
					FArdaDependencyAccess B{P.mBuffer,
					    ToTexture ? EArdaDependencyAccess::Read : EArdaDependencyAccess::Write,
					    ToTexture ? EArdaRHIResourceState::CopySource : EArdaRHIResourceState::CopyDest};
					B.mBufferRange = {P.mLayout.mByteOffset + Row * P.mLayout.mRowPitch,
					    Contiguous ? P.mRowCount * P.mRowBytes : P.mRowBytes};
					N.mAccesses.push_back(B);
				}
				return N;
			};
			D.mRecord = [ToTexture](FArdaDependencyExecutionContext& C, const FTextureTransferParameters& P)
			{
				return ToTexture ? C.GetCommands().CopyBufferToTexture(*C.GetTexture(P.mTexture),
				                       P.mSlice,
				                       *C.GetBuffer(P.mBuffer),
				                       P.mLayout)
				                 : C.GetCommands().CopyTextureToBuffer(*C.GetBuffer(P.mBuffer),
				                       P.mLayout,
				                       *C.GetTexture(P.mTexture),
				                       P.mSlice);
			};
			if (auto S = Registry.Register(eastl::move(D)); !S)
			{
				return S;
			}
		}
		return {};
	}

	TArdaRHIResult<FArdaGraphNodeHandle> AttachArdaTextureToBuffer(FArdaDependencyGraph& Graph,
	    eastl::string Name,
	    FArdaDependencyResourceHandle Texture,
	    const FArdaRHITextureSlice& Slice,
	    FArdaDependencyResourceHandle Buffer,
	    const FArdaRHITextureBufferLayout& Layout)
	{
		return AttachTransfer(Graph, eastl::move(Name), Texture, Slice, Buffer, Layout, false);
	}

	TArdaRHIResult<FArdaGraphNodeHandle> AttachArdaBufferToTexture(FArdaDependencyGraph& Graph,
	    eastl::string Name,
	    FArdaDependencyResourceHandle Texture,
	    const FArdaRHITextureSlice& Slice,
	    FArdaDependencyResourceHandle Buffer,
	    const FArdaRHITextureBufferLayout& Layout)
	{
		return AttachTransfer(Graph, eastl::move(Name), Texture, Slice, Buffer, Layout, true);
	}
}
