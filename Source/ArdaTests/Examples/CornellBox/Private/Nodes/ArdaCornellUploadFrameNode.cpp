#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellUploadFrameNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	FArdaRHIStatus FArdaCornellUploadFrameNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = sizeof(FArdaCornellFrameConstants);
		D.mUsage = EArdaRHIBufferUsage::Constant;
		return C.Buffer(P.mConstants, "Constants", D);
	}

	FArdaDependencyNodeMetadata FArdaCornellUploadFrameNode::GetMetadata()
	{
		return {"cornell.upload_frame", 1};
	}

	eastl::string FArdaCornellUploadFrameNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mConstants);
		Key.Value(reinterpret_cast<uintptr_t>(P.mFrame.get()));
		Key.Value(P.mWorkspaceBytes);
		return Key.Build();
	}

	FArdaRHIStatus FArdaCornellUploadFrameNode::Validate(const FArdaParameters& P)
	{
		if (!P.mFrame)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Upload requires retained frame inputs.");
		}
		return {};
	}

	FArdaDependencyNodeDesc FArdaCornellUploadFrameNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		D.mAccesses = {{P.mConstants, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};

		return D;
	}

	FArdaRHIStatus FArdaCornellUploadFrameNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		auto& Commands = C.GetCommands();

		return Commands.WriteBuffer(*C.GetBuffer(P.mConstants),
		    &P.mFrame->mConstants,
		    sizeof(FArdaCornellFrameConstants));
	}
}
