#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda
{
	using namespace inductor_detail;

	namespace
	{
		FArdaRHIStatus OptimizeOrder(const FArdaInductorGraph& Graph,
		    FArdaInductorCompileResult& Result,
		    bool Memory,
		    uint32_t Budget)
		{
			auto& G = FArdaInductorGraphAccess::State(Graph);
			if (auto S = ValidateNodeOrder(G, Result); !S)
			{
				return S;
			}
			FArdaInductorCompileResult Candidate;
			auto Nodes = Result.mExecutionOrder;
			eastl::sort(Nodes.begin(),
			    Nodes.end(),
			    [&](auto A, auto B)
			    {
				    return G.mTopology.TryGetNode(A)->mPayload.mName < G.mTopology.TryGetNode(B)->mPayload.mName;
			    });
			FArdaScheduleSearch Search(G, eastl::move(Nodes), Candidate);
			if (Memory)
			{
				Search.mObjective = EArdaInductorObjective::Memory;
				Search.mMaxSearchStates = Budget;
				Search.mSearchMode = EArdaInductorSearchMode::Bounded;
			}
			if (!Search.mMaxSearchStates)
			{
				return Error("Memory ordering search budget must be positive.");
			}
			if (auto S = Search.Initialize(); !S)
			{
				return S;
			}
			Search.Greedy(false);
			Search.Greedy(true);
			Search.ImproveIncumbent();
			if (Search.mSearchMode != EArdaInductorSearchMode::Greedy)
			{
				Search.Explore();
			}
			if (auto S = PublishSchedule(Search, Candidate); !S)
			{
				return S;
			}
			Result.mExecutionOrder = eastl::move(Candidate.mExecutionOrder);
			Result.mSchedulesExamined = Candidate.mSchedulesExamined;
			Result.mSearchStatesExamined = Candidate.mSearchStatesExamined;
			Result.mbSearchComplete = Candidate.mbSearchComplete;
			Result.mbSearchExhausted = Candidate.mbSearchExhausted;
			return {};
		}
	}

	FArdaRHIStatus FArdaSearchOrderRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		return OptimizeOrder(Graph, Result, false, 0);
	}

	FArdaRHIStatus FArdaMinimizeMemoryRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		return OptimizeOrder(Graph, Result, true, mSearchBudget);
	}

	FArdaRHIStatus FArdaStableOrderRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		const auto& G = FArdaInductorGraphAccess::State(Graph);
		if (auto S = ValidateNodeOrder(G, Result); !S)
		{
			return S;
		}
		auto Remaining = Result.mExecutionOrder;
		eastl::sort(Remaining.begin(),
		    Remaining.end(),
		    [&](auto A, auto B)
		    {
			    return G.mTopology.TryGetNode(A)->mPayload.mName < G.mTopology.TryGetNode(B)->mPayload.mName;
		    });
		eastl::vector<FArdaGraphNodeHandle> Order;
		while (!Remaining.empty())
		{
			auto Ready = eastl::find_if(Remaining.begin(),
			    Remaining.end(),
			    [&](auto N)
			    {
				    for (auto E : G.mTopology.GetIncomingEdges(N))
				    {
					    if (eastl::find(Remaining.begin(), Remaining.end(), G.mTopology.TryGetEdge(E)->mFrom) !=
					        Remaining.end())
					    {
						    return false;
					    }
				    }
				    return true;
			    });
			if (Ready == Remaining.end())
			{
				return Error("Ordering cannot find a ready node.");
			}
			Order.push_back(*Ready);
			Remaining.erase(Ready);
		}
		Result.mExecutionOrder = eastl::move(Order);
		Result.mSchedulesExamined = 1;
		Result.mbSearchComplete = false;
		return {};
	}

	FArdaRHIStatus FArdaInductorOrderingRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		const auto& G = FArdaInductorGraphAccess::State(Graph);
		if (auto S = ValidateNodeOrder(G, Result); !S)
		{
			return S;
		}
		eastl::vector<FArdaInductorOrderingNode> Nodes;
		for (auto H : Result.mExecutionOrder)
		{
			const auto& N = G.mTopology.TryGetNode(H)->mPayload;
			FArdaInductorOrderingNode Item{H.GetIndex(), N.mName, Graph.GetNodeCost(H), {}};
			for (auto E : G.mTopology.GetIncomingEdges(H))
			{
				Item.mDependencies.push_back(G.mTopology.TryGetEdge(E)->mFrom.GetIndex());
			}
			Nodes.push_back(eastl::move(Item));
		}
		eastl::sort(Nodes.begin(),
		    Nodes.end(),
		    [](const auto& A, const auto& B)
		    {
			    return A.mName < B.mName;
		    });
		auto Ordered = Order(Graph, Nodes);
		if (!Ordered)
		{
			return Ordered.mStatus;
		}
		auto Candidate = Result;
		Candidate.mExecutionOrder.clear();
		for (auto Id : Ordered.mValue)
		{
			const auto Found = eastl::find_if(Result.mExecutionOrder.begin(),
			    Result.mExecutionOrder.end(),
			    [&](auto H)
			    {
				    return H.GetIndex() == Id;
			    });
			if (Found == Result.mExecutionOrder.end())
			{
				return Error("Ordering returned an unknown or culled node.");
			}
			Candidate.mExecutionOrder.push_back(*Found);
		}
		if (auto S = ValidateNodeOrder(G, Candidate); !S)
		{
			return S;
		}
		Result.mExecutionOrder = eastl::move(Candidate.mExecutionOrder);
		Result.mSchedulesExamined = 1;
		Result.mbSearchComplete = false;
		return {};
	}
}
