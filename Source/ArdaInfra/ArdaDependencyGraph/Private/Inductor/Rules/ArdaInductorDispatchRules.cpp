#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda
{
	using namespace inductor_detail;

	FArdaRHIStatus FArdaFlattenGraphRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		const auto Sorted = Graph.GetTopology().TopologicalSort();
		if (!Sorted.IsAcyclic())
		{
			return Error("Cannot flatten a cyclic graph.");
		}
		Result.mExecutionOrder = Sorted.mOrder;
		Result.mCulledNodes.clear();
		return {};
	}

	FArdaRHIStatus FArdaBatchCudaRule::Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const
	{
		const auto& G = FArdaInductorGraphAccess::State(Graph);
		if (auto S = ValidateNodeOrder(G, Result); !S)
		{
			return S;
		}
		BuildCudaBatches(G, Result);
		return {};
	}

	FArdaRHIStatus FArdaFlattenDispatchesRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		const auto& G = FArdaInductorGraphAccess::State(Graph);
		if (auto S = ValidateNodeOrder(G, Result); !S)
		{
			return S;
		}
		const FArdaInductorQueueContext Input{G.mTopology, G.mResources, G.mOptions, G.mDevice, {}};
		if (auto S = ValidateQueueAssignment(Input, Result); !S)
		{
			return S;
		}
		auto Expected = Result;
		BuildCudaBatches(G, Expected);
		if (Expected.mCudaBatches != Result.mCudaBatches)
		{
			return Error("Build CUDA batches before flattening dispatches.");
		}
		Result.mDispatches.clear();
		for (auto& Queue : Result.mQueueDispatches)
		{
			Queue.clear();
		}
		Result.mCudaStreams.clear();
		Result.mDispatchDependencies.clear();
		Result.mFences.clear();
		eastl::unordered_set<uint32_t> Emitted;
		for (size_t I = 0; I < Result.mExecutionOrder.size(); ++I)
		{
			const auto H = Result.mExecutionOrder[I];
			const auto& Node = G.mTopology.TryGetNode(H)->mPayload;
			if (IsDeclaration(Node) || Emitted.count(H.GetIndex()))
			{
				continue;
			}
			FArdaInductorDispatch Dispatch;
			Dispatch.mQueue = Result.mQueues[I];
			Dispatch.mNodes.push_back(H);
			const auto Id = static_cast<uint32_t>(Result.mDispatches.size());
			if (Node.mDefinition->mKind == EArdaDependencyNodeKind::Cuda)
			{
				const auto Batch = eastl::find_if(Result.mCudaBatches.begin(),
				    Result.mCudaBatches.end(),
				    [&](const auto& B)
				    {
					    return B.front() == H;
				    });
				if (Batch == Result.mCudaBatches.end())
				{
					return Error("Missing CUDA batch head.");
				}
				Dispatch.mNodes = *Batch;
				Dispatch.mCudaStream = 0;
				if (Result.mCudaStreams.empty())
				{
					Result.mCudaStreams.resize(1);
				}
				Result.mCudaStreams[0].push_back(Id);
			}
			else
			{
				Result.mQueueDispatches[GetArdaRHIQueueIndex(Dispatch.mQueue)].push_back(Id);
			}
			for (auto N : Dispatch.mNodes)
			{
				Emitted.insert(N.GetIndex());
			}
			Result.mDispatches.push_back(eastl::move(Dispatch));
		}
		return {};
	}

	FArdaRHIStatus FArdaResolveFencesRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		const auto& G = FArdaInductorGraphAccess::State(Graph);
		if (auto S = ValidateNodeOrder(G, Result); !S)
		{
			return S;
		}
		eastl::unordered_map<uint32_t, uint32_t> Dispatches;
		for (uint32_t I = 0; I < Result.mDispatches.size(); ++I)
		{
			for (auto N : Result.mDispatches[I].mNodes)
			{
				if (!G.mTopology.ContainsNode(N) || !Dispatches.emplace(N.GetIndex(), I).second)
				{
					return Error("Dispatches contain a stale or duplicate node.");
				}
			}
		}
		eastl::unordered_map<uint32_t, eastl::vector<FArdaGraphNodeHandle>> Predecessors;
		for (auto E : G.mTopology.GetEdges())
		{
			const auto& Edge = *G.mTopology.TryGetEdge(E);
			Predecessors[Edge.mTo.GetIndex()].push_back(Edge.mFrom);
		}
		for (const auto& E : Result.mMemoryDependencies)
		{
			Predecessors[E.second.GetIndex()].push_back(E.first);
		}
		Result.mDispatchDependencies.clear();
		Result.mFences.clear();
		const auto Add = [&](uint32_t P, uint32_t C) -> FArdaRHIStatus
		{
			if (P == C)
			{
				return {};
			}
			if (P >= C || C >= Result.mDispatches.size())
			{
				return Error("Dispatch dependency opposes flattened order.");
			}
			const auto Pair = eastl::make_pair(P, C);
			if (eastl::find(Result.mDispatchDependencies.begin(), Result.mDispatchDependencies.end(), Pair) ==
			    Result.mDispatchDependencies.end())
			{
				Result.mDispatchDependencies.push_back(Pair);
			}
			return {};
		};
		for (auto C : Result.mExecutionOrder)
		{
			const auto Consumer = Dispatches.find(C.GetIndex());
			if (Consumer == Dispatches.end())
			{
				if (!IsDeclaration(G.mTopology.TryGetNode(C)->mPayload))
				{
					return Error("An executable node has no dispatch.");
				}
				continue;
			}
			auto Pending = Predecessors[C.GetIndex()];
			eastl::unordered_set<uint32_t> Seen;
			while (!Pending.empty())
			{
				const auto P = Pending.back();
				Pending.pop_back();
				if (!Seen.insert(P.GetIndex()).second)
				{
					continue;
				}
				const auto Producer = Dispatches.find(P.GetIndex());
				if (Producer != Dispatches.end())
				{
					if (auto S = Add(Producer->second, Consumer->second); !S)
					{
						return S;
					}
				}
				else
				{
					const auto& Earlier = Predecessors[P.GetIndex()];
					Pending.insert(Pending.end(), Earlier.begin(), Earlier.end());
				}
			}
		}
		const auto Chain = [&](const auto& Lists) -> FArdaRHIStatus
		{
			for (const auto& List : Lists)
			{
				for (size_t I = 1; I < List.size(); ++I)
				{
					if (auto S = Add(List[I - 1], List[I]); !S)
					{
						return S;
					}
				}
			}
			return {};
		};
		if (auto S = Chain(Result.mQueueDispatches); !S)
		{
			return S;
		}
		if (auto S = Chain(Result.mCudaStreams); !S)
		{
			return S;
		}
		for (const auto& E : Result.mDispatchDependencies)
		{
			const auto& P = Result.mDispatches[E.first];
			const auto& C = Result.mDispatches[E.second];
			if (P.mQueue != C.mQueue || P.mCudaStream != C.mCudaStream)
			{
				Result.mFences.push_back({E.first, E.second});
			}
		}
		return {};
	}
}
