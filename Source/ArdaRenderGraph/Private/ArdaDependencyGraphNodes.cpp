#include "ArdaDependencyGraphNodes.h"
#include <mutex>

namespace arda
{
	FArdaRHIStatus InitializeArdaTextureTransferNodes(FArdaNodeRegistry& Registry);

	namespace
	{
		template <class T>
		void KeyValue(eastl::string& K, T V)
		{
			K.append(reinterpret_cast<const char*>(&V), sizeof(V));
		}

		void KeyResource(eastl::string& K, FArdaDependencyResourceHandle H)
		{
			KeyValue(K, H.mGraph);
			KeyValue(K, H.mIndex);
			KeyValue(K, H.mGeneration);
		}

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

	eastl::string FArdaGraphUploadNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string K;
		KeyResource(K, P.mDestination);
		KeyValue(K, P.mOffset);
		if (!P.mBytes.empty())
		{
			K.append(reinterpret_cast<const char*>(P.mBytes.data()), P.mBytes.size());
		}
		return K;
	}

	FArdaDependencyNodeDesc FArdaGraphUploadNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		auto A = Access(P.mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest);
		A.mBufferRange = {P.mOffset, P.mBytes.size()};
		D.mAccesses.push_back(A);
		return D;
	}

	FArdaRHIStatus FArdaGraphUploadNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		auto B = C.GetBuffer(P.mDestination);
		return C.GetCommands().WriteBuffer(*B, P.mBytes.data(), P.mBytes.size(), P.mOffset);
	}

	FArdaDependencyNodeMetadata FArdaGraphCopyNode::GetMetadata()
	{
		return {"arda.copy-buffer", 1};
	}

	eastl::string FArdaGraphCopyNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string K;
		KeyResource(K, P.mSource);
		KeyResource(K, P.mDestination);
		KeyValue(K, P.mByteSize);
		KeyValue(K, P.mSourceOffset);
		KeyValue(K, P.mDestinationOffset);
		return K;
	}

	FArdaDependencyNodeDesc FArdaGraphCopyNode::Describe(const FParameters& P, const FState& Prepared)
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
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
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

	eastl::string FArdaGraphClearNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string K;
		KeyResource(K, P.mDestination);
		KeyValue(K, P.mValue);
		return K;
	}

	FArdaDependencyNodeDesc FArdaGraphClearNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses.push_back(
		    Access(P.mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess));
		return D;
	}

	FArdaRHIStatus FArdaGraphClearNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		return C.GetCommands().ClearBufferUInt(*C.GetBuffer(P.mDestination), P.mValue);
	}

	FArdaDependencyNodeMetadata FArdaGraphReadbackNode::GetMetadata()
	{
		return {"arda.readback", 1};
	}

	eastl::string FArdaGraphReadbackNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string K;
		KeyResource(K, P.mSource);
		KeyValue(K, reinterpret_cast<uintptr_t>(P.mDestination.get()));
		return K;
	}

	FArdaDependencyNodeDesc FArdaGraphReadbackNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mbSideEffect = true;
		D.mAccesses.push_back(Access(P.mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource));
		return D;
	}

	FArdaRHIStatus FArdaGraphReadbackNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		return C.ReadbackBuffer(P.mSource, P.mDestination);
	}

	FArdaDependencyNodeMetadata FArdaGraphSyncNode::GetMetadata()
	{
		return {"arda.sync", 1};
	}

	eastl::string FArdaGraphSyncNode::GetCanonicalKey(const FParameters& P)
	{
		return eastl::string{};
	}

	FArdaDependencyNodeDesc FArdaGraphSyncNode::Describe(const FParameters& P, const FState& Prepared)
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
