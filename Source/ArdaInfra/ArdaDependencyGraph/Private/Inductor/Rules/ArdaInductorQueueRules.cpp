#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Rules/ArdaInductorQueueRules.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"

namespace arda::inductor_detail
{
	void AssignNativeQueues(const FArdaInductorQueueContext& G,
	    FArdaInductorCompileResult& R,
	    bool GraphicsOnly,
	    bool EnableCopy = true,
	    bool EnableCompute = true)
	{
		eastl::unordered_map<uint32_t, eastl::vector<FArdaHandle>> MemorySuccessors;
		for (const auto& Edge : R.mMemoryDependencies)
		{
			MemorySuccessors[Edge.first.GetIndex()].push_back(Edge.second);
		}
		const auto IsOrdered = [&](FArdaHandle From, FArdaHandle To)
		{
			if (MemorySuccessors.empty())
			{
				return G.mTopology.IsReachable(From, To);
			}
			eastl::vector<FArdaHandle> Pending{From};
			eastl::unordered_set<uint32_t> Seen;
			for (size_t Index = 0; Index < Pending.size(); ++Index)
			{
				const auto Node = Pending[Index];
				if (Node == To)
				{
					return true;
				}
				if (!Seen.insert(Node.GetIndex()).second)
				{
					continue;
				}
				for (const auto Edge : G.mTopology.GetOutgoingEdges(Node))
				{
					Pending.push_back(G.mTopology.TryGetEdge(Edge)->mTo);
				}
				const auto Added = MemorySuccessors.find(Node.GetIndex());
				if (Added != MemorySuccessors.end())
				{
					Pending.insert(Pending.end(), Added->second.begin(), Added->second.end());
				}
			}
			return false;
		};
		const auto QueueAvailable = [&](EArdaRHIQueueType Queue)
		{
			return !G.mDevice ||
			    (G.mDevice->GetCapabilities().IsQueueSupported(Queue) &&
			        G.mDevice->GetCapabilities().mQueues.mbGpuWaits);
		};
		const bool CopyAvailable =
		    !GraphicsOnly && EnableCopy && G.mOptions.mbEnableCopyQueue && QueueAvailable(EArdaRHIQueueType::Copy);
		const bool ComputeAvailable = !GraphicsOnly && EnableCompute && G.mOptions.mbEnableAsyncCompute &&
		    QueueAvailable(EArdaRHIQueueType::Compute);
		// AS storage has no cross-family ownership API yet; compute commands remain valid on Graphics.
		const auto AsyncCandidate = [&](FArdaHandle Handle)
		{
			const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			return Node.mDefinition->mKind == EArdaDependencyNodeKind::Compute &&
			    eastl::none_of(Node.mDesc.mAccesses.begin(),
			        Node.mDesc.mAccesses.end(),
			        [&](const auto& Access)
			        {
				        return bool(G.mResources[Access.mResource.mIndex].mExternalAccelerationStructure) ||
				            (static_cast<uint32_t>(
				                 NormalizeStateForPipeline(Access.mState, EArdaRHIQueueType::Compute)) &
				                GraphicsOnlyMask) != 0;
			        });
		};
		R.mQueues.assign(R.mExecutionOrder.size(), EArdaRHIQueueType::Graphics);
		eastl::unordered_map<uint32_t, size_t> Position;
		for (size_t I = 0; I < R.mExecutionOrder.size(); ++I)
		{
			Position.emplace(R.mExecutionOrder[I].GetIndex(), I);
		}
		eastl::unordered_set<uint32_t> Visited;
		for (size_t I = 0; I < R.mExecutionOrder.size(); ++I)
		{
			auto H = R.mExecutionOrder[I];
			const auto& N = G.mTopology.TryGetNode(H)->mPayload;
			if (N.mDefinition->mKind == EArdaDependencyNodeKind::Copy && CopyAvailable &&
			    eastl::all_of(N.mDesc.mAccesses.begin(),
			        N.mDesc.mAccesses.end(),
			        [](const auto& Access)
			        {
				        return (static_cast<uint32_t>(Access.mState) & ~CopyMask) == 0;
			        }))
			{
				R.mQueues[I] = EArdaRHIQueueType::Copy;
			}
			if (!AsyncCandidate(H) || !ComputeAvailable || Visited.count(H.GetIndex()))
			{
				continue;
			}
			eastl::vector<FArdaHandle> Chain{H};
			Visited.insert(H.GetIndex());
			for (size_t C = 0; C < Chain.size(); ++C)
			{
				for (auto E : G.mTopology.GetOutgoingEdges(Chain[C]))
				{
					auto Next = G.mTopology.TryGetEdge(E)->mTo;
					if (Position.count(Next.GetIndex()) && AsyncCandidate(Next) &&
					    Visited.insert(Next.GetIndex()).second)
					{
						Chain.push_back(Next);
					}
				}
			}
			size_t First = I, Last = I, Join = R.mExecutionOrder.size();
			for (auto C : Chain)
			{
				First = eastl::min(First, Position[C.GetIndex()]);
				Last = eastl::max(Last, Position[C.GetIndex()]);
				for (auto E : G.mTopology.GetOutgoingEdges(C))
				{
					auto D = G.mTopology.TryGetEdge(E)->mTo;
					if (Position.count(D.GetIndex()) && !AsyncCandidate(D))
					{
						Join = eastl::min(Join, Position[D.GetIndex()]);
					}
				}
			}
			double IndependentCost = 0;
			// Independent graphics recorded before the first compute node can still overlap on the GPU.
			for (size_t J = 0; J < Join; ++J)
			{
				auto D = R.mExecutionOrder[J];
				if (G.mTopology.TryGetNode(D)->mPayload.mDefinition->mKind != EArdaDependencyNodeKind::Graphics)
				{
					continue;
				}
				bool Independent = true;
				for (auto C : Chain)
				{
					Independent &= !IsOrdered(C, D) && !IsOrdered(D, C);
				}
				if (Independent)
				{
					IndependentCost += G.mNodeCost(D);
				}
			}
			if (Chain.size() >= G.mOptions.mMinimumAsyncChain && Join > Last &&
			    IndependentCost >= G.mOptions.mMinimumAsyncSlack)
			{
				for (auto C : Chain)
				{
					R.mQueues[Position[C.GetIndex()]] = EArdaRHIQueueType::Compute;
				}
			}
		}
	}

	void BuildCudaBatches(const FArdaDependencyGraph::FArdaImpl& Graph, FArdaInductorCompileResult& R)
	{
		R.mCudaBatches.clear();
		eastl::vector<FArdaHandle> Batch;
		for (const auto Handle : R.mExecutionOrder)
		{
			const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
			if (IsDeclaration(Node))
			{
				continue;
			}
			if (Node.mDefinition->mKind == EArdaDependencyNodeKind::Cuda)
			{
				Batch.push_back(Handle);
			}
			else if (!Batch.empty())
			{
				R.mCudaBatches.push_back(eastl::move(Batch));
				Batch.clear();
			}
		}
		if (!Batch.empty())
		{
			R.mCudaBatches.push_back(eastl::move(Batch));
		}
	}

	FArdaRHIStatus ValidateQueueAssignment(const FArdaInductorQueueContext& Graph,
	    const FArdaInductorCompileResult& Result)
	{
		if (Result.mQueues.size() != Result.mExecutionOrder.size())
		{
			return Error("A queue rule must assign exactly one queue per scheduled node.");
		}
		for (size_t Index = 0; Index < Result.mExecutionOrder.size(); ++Index)
		{
			const auto Handle = Result.mExecutionOrder[Index];
			const auto* Record = Graph.mTopology.TryGetNode(Handle);
			if (!Record || !Record->mPayload.mDefinition)
			{
				return Error("A queue rule received a stale scheduled node.");
			}
			const auto& Node = Record->mPayload;
			const auto Queue = Result.mQueues[Index];
			if (GetArdaRHIQueueIndex(Queue) >= ArdaRHIQueueTypeCount)
			{
				return Error("A queue rule selected an invalid queue type.");
			}
			if (Graph.mDevice &&
			    (!Graph.mDevice->GetCapabilities().IsQueueSupported(Queue) ||
			        (Queue != EArdaRHIQueueType::Graphics && !Graph.mDevice->GetCapabilities().mQueues.mbGpuWaits)))
			{
				return Error("A queue rule selected a queue without the required device support or GPU waits.");
			}
			const auto Kind = Node.mDefinition->mKind;
			if ((Queue == EArdaRHIQueueType::Copy &&
			        (Kind != EArdaDependencyNodeKind::Copy || !Graph.mOptions.mbEnableCopyQueue)) ||
			    (Queue == EArdaRHIQueueType::Compute &&
			        ((Kind != EArdaDependencyNodeKind::Compute && Kind != EArdaDependencyNodeKind::Copy) ||
			            !Graph.mOptions.mbEnableAsyncCompute)))
			{
				return Error("A queue rule assigned a node to an incompatible or disabled queue.");
			}
			for (const auto& Access : Node.mDesc.mAccesses)
			{
				if (Access.mResource.mIndex >= Graph.mResources.size())
				{
					return Error("A queue rule received an invalid logical resource.");
				}
				if ((Queue == EArdaRHIQueueType::Copy && (static_cast<uint32_t>(Access.mState) & ~CopyMask)) ||
				    (Queue == EArdaRHIQueueType::Compute &&
				        ((static_cast<uint32_t>(NormalizeStateForPipeline(Access.mState, Queue)) & GraphicsOnlyMask) ||
				            Graph.mResources[Access.mResource.mIndex].mExternalAccelerationStructure)))
				{
					return Error("A queue rule selected a queue incompatible with the node's resource states.");
				}
			}
		}

		return {};
	}

	FArdaRHIStatus ApplyQueuePolicy(const FArdaInductorQueueRule& Rule,
	    const FArdaDependencyGraph::FArdaImpl& Graph,
	    FArdaInductorCompileResult& Result)
	{
		const FArdaInductorQueueContext Inputs{Graph.mTopology,
		    Graph.mResources,
		    Graph.mOptions,
		    Graph.mDevice,
		    [&](FArdaGraphNodeHandle Handle)
		    {
			    const auto* Node = Graph.mTopology.TryGetNode(Handle);
			    return Node ? NodeCost(Graph, Node->mPayload) : 0.0;
		    }};
		auto Candidate = Result;
		try
		{
			if (auto Status = Rule.Assign(Inputs, Candidate); !Status)
			{
				return Status;
			}
		}
		catch (...)
		{
			return Error("The Inductor queue rule threw an exception; compilation was not published.");
		}
		if (Candidate.mExecutionOrder != Result.mExecutionOrder ||
		    Candidate.mMemoryDependencies != Result.mMemoryDependencies ||
		    Candidate.mCudaBatches != Result.mCudaBatches)
		{
			return Error("A queue rule must preserve the selected node order and memory dependencies.");
		}
		if (auto Status = ValidateQueueAssignment(Inputs, Candidate); !Status)
		{
			return Status;
		}
		Result.mQueues = eastl::move(Candidate.mQueues);

		return {};
	}

	FArdaRHIStatus AssignQueues(const FArdaDependencyGraph::FArdaImpl& Graph, FArdaInductorCompileResult& Result)
	{
		Result.mQueues.assign(Result.mExecutionOrder.size(), EArdaRHIQueueType::Graphics);
		for (const auto& Rule : Graph.mOptions.mRules.mCompileRules)
		{
			if (const auto* Queue = dynamic_cast<const FArdaInductorQueueRule*>(Rule.get()))
			{
				if (auto S = ApplyQueuePolicy(*Queue, Graph, Result); !S)
				{
					return S;
				}
			}
		}
		BuildCudaBatches(Graph, Result);
		return {};
	}
}

namespace arda
{
	FArdaRHIStatus FArdaInductorQueueRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		const auto& G = FArdaInductorGraphAccess::State(Graph);
		if (auto S = inductor_detail::ValidateNodeOrder(G, Result); !S)
		{
			return S;
		}
		return inductor_detail::ApplyQueuePolicy(*this, G, Result);
	}

	FArdaRHIStatus FArdaAssignQueuesRule::Assign(const FArdaInductorQueueContext& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		inductor_detail::AssignNativeQueues(Graph, Result, false, mbEnableCopy, mbEnableCompute);
		return {};
	}

	FArdaRHIStatus FArdaGraphicsQueueRule::Assign(const FArdaInductorQueueContext& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		inductor_detail::AssignNativeQueues(Graph, Result, true);
		return {};
	}
}
