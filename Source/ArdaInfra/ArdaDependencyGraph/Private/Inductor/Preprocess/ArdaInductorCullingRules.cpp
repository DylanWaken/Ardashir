#include "Inductor/Preprocess/ArdaInductorCullingRules.h"
#include "Inductor/Rules/ArdaInductorGraphInternal.h"

namespace arda
{
	FArdaRHIStatus FArdaCullUnreachableRule::Preprocess(FArdaInductorGraph& Graph) const
	{
		auto& G = FArdaInductorGraphAccess::State(Graph);
		eastl::unordered_set<FArdaGraphNodeHandle, FArdaGraphNodeHandleHash> Live;
		eastl::vector<FArdaGraphNodeHandle> Stack;
		for (auto Handle : G.mTopology.GetNodes())
		{
			const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			bool bRoot = Node.mDesc.mbSideEffect;
			for (const auto& Access : Node.mDesc.mAccesses)
			{
				const auto Resource = G.ResolveResource(Access.mResource);
				if (!G.HasResource(Resource))
				{
					return inductor_detail::Error("Culling requires valid resource accesses.");
				}
				const auto& Description = G.mResources[Resource.mIndex];
				bRoot |= Access.mAccess != EArdaDependencyAccess::Read &&
				    (Description.mbOutput || Description.mbPersistent || Description.IsExternal());
			}
			if (bRoot)
			{
				Stack.push_back(Handle);
			}
		}
		while (!Stack.empty())
		{
			const auto Handle = Stack.back();
			Stack.pop_back();
			if (!Live.insert(Handle).second)
			{
				continue;
			}
			for (auto Edge : G.mTopology.GetIncomingEdges(Handle))
			{
				Stack.push_back(G.mTopology.TryGetEdge(Edge)->mFrom);
			}
		}
		for (auto Handle : G.mTopology.GetNodes())
		{
			G.mTopology.TryGetNode(Handle)->mPayload.mbCulled = !Live.count(Handle);
		}
		return {};
	}

	FArdaRHIStatus FArdaKeepAllNodesRule::Preprocess(FArdaInductorGraph& Graph) const
	{
		for (auto Handle : Graph.GetTopology().GetNodes())
		{
			Graph.GetTopology().TryGetNode(Handle)->mPayload.mbCulled = false;
		}
		return {};
	}
}
