#include "ArdaDependencyGraphNodes.h"
#include <mutex>

namespace arda
{
	FArdaRHIStatus InitializeArdaTextureTransferNodes(FArdaNodeRegistry& Registry);

	namespace
	{
		FArdaDependencyAccess Access(FArdaDependencyResourceHandle H, EArdaDependencyAccess A, EArdaRHIResourceState S)
		{
			FArdaDependencyAccess R;
			R.mResource = H;
			R.mAccess = A;
			R.mState = S;
			return R;
		}
	}

	FArdaDependencyNodeMetadata FArdaGraphUploadNode::GetMetadata()
	{
		return {"arda.upload", 1};
	}

	eastl::string FArdaGraphUploadNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder K;
		K.Resource(P.mDestination);
		K.Value(P.mOffset);
		K.Bytes(P.mBytes.data(), P.mBytes.size());
		return K.Build();
	}

	FArdaDependencyNodeDesc FArdaGraphUploadNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		auto A = Access(P.mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest);
		A.mBufferRange = {P.mOffset, P.mBytes.size()};
		D.mAccesses.push_back(A);
		return D;
	}

	FArdaRHIStatus FArdaGraphUploadNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		auto B = C.GetBuffer(P.mDestination);
		return C.GetCommands().WriteBuffer(*B, P.mBytes.data(), P.mBytes.size(), P.mOffset);
	}

	FArdaDependencyNodeMetadata FArdaGraphCopyNode::GetMetadata()
	{
		return {"arda.copy-buffer", 1};
	}

	FArdaRHIStatus FArdaGraphCopyNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		const auto* Source = C.Find(P.mSource);
		if (!Source || Source->mbTexture || Source->mExternalAccelerationStructure)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Copy needs a valid source buffer.");
		}
		if (!P.mByteSize || P.mDestinationOffset > UINT64_MAX - P.mByteSize)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Invalid copy output size.");
		}
		auto D = Source->mBuffer;
		// An inferred output needs ordinary GPU-writable storage, independent of the source's backing heap.
		D.mCpuAccess = EArdaRHICpuAccess::None;
		D.mbTiled = false;
		if (P.mDestination)
		{
			const auto* Destination = C.Find(P.mDestination);
			if (!Destination || Destination->mbTexture || Destination->mExternalAccelerationStructure)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Copy needs a valid destination buffer.");
			}
			D = Destination->mBuffer;
		}
		D.mByteSize = P.mDestinationOffset + P.mByteSize;
		return C.Buffer(P.mDestination, "Destination", D);
	}

	eastl::string FArdaGraphCopyNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder K;
		K.Resource(P.mSource);
		K.Resource(P.mDestination);
		K.Value(P.mByteSize);
		K.Value(P.mSourceOffset);
		K.Value(P.mDestinationOffset);
		return K.Build();
	}

	FArdaDependencyNodeDesc FArdaGraphCopyNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		auto S = Access(P.mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource);
		auto T = Access(P.mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest);
		S.mBufferRange = {P.mSourceOffset, P.mByteSize};
		T.mBufferRange = {P.mDestinationOffset, P.mByteSize};
		D.mAccesses = {S, T};
		return D;
	}

	FArdaRHIStatus FArdaGraphCopyNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		return C.GetCommands().CopyBuffer(*C.GetBuffer(P.mDestination),
		    P.mDestinationOffset,
		    *C.GetBuffer(P.mSource),
		    P.mSourceOffset,
		    P.mByteSize);
	}

	FArdaDependencyNodeMetadata FArdaGraphClearNode::GetMetadata()
	{
		return {"arda.clear-buffer", 1};
	}

	eastl::string FArdaGraphClearNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder K;
		K.Resource(P.mDestination);
		K.Value(P.mValue);
		return K.Build();
	}

	FArdaDependencyNodeDesc FArdaGraphClearNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses.push_back(
		    Access(P.mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess));
		return D;
	}

	FArdaRHIStatus FArdaGraphClearNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		return C.GetCommands().ClearBufferUInt(*C.GetBuffer(P.mDestination), P.mValue);
	}

	FArdaDependencyNodeMetadata FArdaGraphReadbackNode::GetMetadata()
	{
		return {"arda.readback", 1};
	}

	eastl::string FArdaGraphReadbackNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder K;
		K.Resource(P.mSource);
		K.Value(reinterpret_cast<uintptr_t>(P.mDestination.get()));
		return K.Build();
	}

	FArdaDependencyNodeDesc FArdaGraphReadbackNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mbSideEffect = true;
		D.mAccesses.push_back(Access(P.mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource));
		return D;
	}

	FArdaRHIStatus FArdaGraphReadbackNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		return C.ReadbackBuffer(P.mSource, P.mDestination);
	}

	FArdaDependencyNodeMetadata FArdaGraphSyncNode::GetMetadata()
	{
		return {"arda.sync", 1};
	}

	eastl::string FArdaGraphSyncNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return eastl::string{};
	}

	FArdaDependencyNodeDesc FArdaGraphSyncNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		return FArdaDependencyNodeDesc{};
	}

	FArdaRHIStatus InitializeArdaBuiltinNodes(FArdaNodeRegistry& Registry)
	{
		static std::once_flag Once;
		static FArdaRHIStatus Status;
		std::call_once(Once,
		    [&Registry]
		    {
			    if (Status)
			    {
				    Status = FArdaGraphUploadNode::Register(Registry);
			    }
			    if (Status)
			    {
				    Status = FArdaGraphCopyNode::Register(Registry);
			    }
			    if (Status)
			    {
				    Status = FArdaGraphClearNode::Register(Registry);
			    }
			    if (Status)
			    {
				    Status = FArdaGraphReadbackNode::Register(Registry);
			    }
			    if (Status)
			    {
				    Status = FArdaGraphSyncNode::Register(Registry);
			    }
			    if (Status)
			    {
				    Status = InitializeArdaTextureTransferNodes(Registry);
			    }
		    });
		return Status;
	}

	FArdaRHIStatus RegisterArdaBuiltinNodes()
	{
		return InitializeArdaBuiltinNodes(FArdaNodeRegistry::Get());
	}
}
