#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellUploadFrameNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	FArdaDependencyNodeMetadata FArdaCornellUploadFrameNode::GetMetadata()
	{
		return {"cornell.upload_frame", 1};
	}

	eastl::string FArdaCornellUploadFrameNode::GetCanonicalKey(const FParameters& P)
	{
		return MakeCornellNodeKey(P);
	}

	FArdaRHIStatus FArdaCornellUploadFrameNode::Validate(const FParameters& P)
	{
		if (!P.mFrame)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Upload requires retained frame inputs.");
		}
		return {};
	}

	FArdaDependencyNodeDesc FArdaCornellUploadFrameNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		const auto Read = [&](uint32_t I, EArdaRHIResourceState State)
		{
			D.mAccesses.push_back({P.mResources[I], EArdaDependencyAccess::Read, State});
		};
		const auto Write = [&](uint32_t I, EArdaRHIResourceState State)
		{
			D.mAccesses.push_back({P.mResources[I], EArdaDependencyAccess::Write, State});
		};
		Write(0, EArdaRHIResourceState::CopyDest);

		return D;
	}

	FArdaRHIStatus FArdaCornellUploadFrameNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		auto& Commands = C.GetCommands();

		return Commands.WriteBuffer(*C.GetBuffer(P.mResources[0]),
		    &P.mFrame->mConstants,
		    sizeof(FArdaCornellFrameConstants));
	}
}
