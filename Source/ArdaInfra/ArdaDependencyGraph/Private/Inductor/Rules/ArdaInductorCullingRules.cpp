#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda::inductor_detail
{
	bool Writes(const FArdaDependencyAccess& A)
	{
		return A.mAccess != EArdaDependencyAccess::Read;
	}

	eastl::vector<FArdaHandle> LiveNodes(FArdaDependencyGraph::FArdaImpl& G, FArdaInductorCompileResult& Result)
	{
		eastl::unordered_set<uint32_t> Live;
		eastl::vector<FArdaHandle> Stack;
		for (auto H : G.mTopology.GetNodes())
		{
			const auto& N = G.mTopology.TryGetNode(H)->mPayload;
			bool Root = N.mDesc.mbSideEffect;
			for (const auto& A : N.mDesc.mAccesses)
			{
				const auto& R = G.mResources[A.mResource.mIndex];
				Root |= Writes(A) &&
				    (R.mbOutput || R.mbPersistent || R.mExternalBuffer || R.mExternalTexture ||
				        R.mExternalAccelerationStructure);
			}
			if (Root)
			{
				Stack.push_back(H);
			}
		}
		while (!Stack.empty())
		{
			auto H = Stack.back();
			Stack.pop_back();
			if (!Live.insert(H.GetIndex()).second)
			{
				continue;
			}
			for (auto E : G.mTopology.GetIncomingEdges(H))
			{
				Stack.push_back(G.mTopology.TryGetEdge(E)->mFrom);
			}
		}
		eastl::vector<FArdaHandle> Nodes;
		for (auto H : G.mTopology.GetNodes())
		{
			if (Live.count(H.GetIndex()))
			{
				Nodes.push_back(H);
			}
			else
			{
				Result.mCulledNodes.push_back(H);
			}
		}
		eastl::sort(Nodes.begin(),
		    Nodes.end(),
		    [&](auto A, auto B)
		    {
			    return G.mTopology.TryGetNode(A)->mPayload.mName < G.mTopology.TryGetNode(B)->mPayload.mName;
		    });
		return Nodes;
	}

}

namespace arda
{
	FArdaRHIStatus FArdaCullUnreachableRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		auto& G = FArdaInductorGraphAccess::State(Graph);
		if (auto S = inductor_detail::ValidateNodeOrder(G, Result); !S)
		{
			return S;
		}
		FArdaInductorCompileResult Live;
		const auto Nodes = inductor_detail::LiveNodes(G, Live);
		Result.mExecutionOrder.erase(eastl::remove_if(Result.mExecutionOrder.begin(),
		                                 Result.mExecutionOrder.end(),
		                                 [&](auto N)
		                                 {
			                                 return eastl::find(Nodes.begin(), Nodes.end(), N) == Nodes.end();
		                                 }),
		    Result.mExecutionOrder.end());
		Result.mCulledNodes = G.mTopology.GetNodes();
		Result.mCulledNodes.erase(eastl::remove_if(Result.mCulledNodes.begin(),
		                              Result.mCulledNodes.end(),
		                              [&](auto N)
		                              {
			                              return eastl::find(Result.mExecutionOrder.begin(),
			                                         Result.mExecutionOrder.end(),
			                                         N) != Result.mExecutionOrder.end();
		                              }),
		    Result.mCulledNodes.end());
		return {};
	}

	FArdaRHIStatus FArdaKeepAllNodesRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		return FArdaFlattenGraphRule{}.Apply(Graph, Result);
	}
}
