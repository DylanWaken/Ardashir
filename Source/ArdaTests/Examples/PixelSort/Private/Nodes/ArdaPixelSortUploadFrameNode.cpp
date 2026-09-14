#include "Nodes/ArdaPixelSortUploadFrameNode.h"
#include "ArdaPixelSortNodeInternal.h"

namespace arda
{
	struct FArdaPixelSortConstants
	{
		uint32_t mWidth;
		uint32_t mHeight;
		float mTime;
		uint32_t mOriginal;
	};

	static_assert(sizeof(FArdaPixelSortConstants) == 16);

	FArdaRHIStatus FArdaPixelSortUploadFrameNode::DeclareResources(FArdaDependencyResourceContext& C,
	    FArdaParameters& P)
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = 256;
		D.mUsage = EArdaRHIBufferUsage::Constant;
		return C.Buffer(P.mConstants, "Constants", D);
	}

	FArdaDependencyNodeMetadata FArdaPixelSortUploadFrameNode::GetMetadata()
	{
		return {"example.pixel-sort.upload", 1};
	}

	eastl::string FArdaPixelSortUploadFrameNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(P.mConstants)
		    .Value(reinterpret_cast<uintptr_t>(P.mInput.get()))
		    .Value(P.mWidth)
		    .Value(P.mHeight)
		    .Build();
	}

	FArdaRHIStatus FArdaPixelSortUploadFrameNode::Validate(const FArdaParameters& P)
	{
		if (!P.mInput)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "PixelSort frame upload requires dynamic upload inputs.");
		}
		return ValidatePixelSortExtent(P.mWidth, P.mHeight);
	}

	FArdaDependencyNodeDesc FArdaPixelSortUploadFrameNode::Describe(const FArdaParameters& P,
	    const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mConstants, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};

		return D;
	}

	FArdaRHIStatus FArdaPixelSortUploadFrameNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		const FArdaPixelSortConstants Constants{P.mWidth, P.mHeight, P.mInput->mTime, P.mInput->mbOriginal ? 1u : 0u};
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P.mConstants), &Constants, sizeof(Constants));
	}
}
