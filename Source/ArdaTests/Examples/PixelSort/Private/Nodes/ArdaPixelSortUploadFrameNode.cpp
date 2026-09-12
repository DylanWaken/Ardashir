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

	FArdaDependencyNodeMetadata FArdaPixelSortUploadFrameNode::GetMetadata()
	{
		return {"example.pixel-sort.upload", 1};
	}

	eastl::string FArdaPixelSortUploadFrameNode::GetCanonicalKey(const FParameters& P)
	{
		return MakePixelSortNodeKey(P);
	}

	FArdaRHIStatus FArdaPixelSortUploadFrameNode::Validate(const FParameters& P)
	{
		if (!P.mInput || !P.mWidth || !P.mHeight)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "PixelSort requires frame inputs and a nonempty extent.");
		}
		return {};
	}

	FArdaDependencyNodeDesc FArdaPixelSortUploadFrameNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mConstants, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};

		return D;
	}

	FArdaRHIStatus FArdaPixelSortUploadFrameNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{

		const FArdaPixelSortConstants Constants{P.mWidth, P.mHeight, P.mInput->mTime, P.mInput->mbOriginal ? 1u : 0u};
		return C.GetCommands().WriteBuffer(*C.GetBuffer(P.mConstants), &Constants, sizeof(Constants));
	}
}
