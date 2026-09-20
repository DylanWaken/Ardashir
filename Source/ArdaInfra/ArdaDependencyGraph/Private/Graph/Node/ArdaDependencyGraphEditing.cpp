#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaDependencyGraphStatus.h"
#include "Inductor/Compiler/ArdaInductor.h"
#include "Inductor/Runtime/ArdaInductorRuntime.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"
#include "Inductor/Rules/ArdaDependencyHazards.h"
#include <EASTL/unordered_set.h>
#include <cmath>

namespace arda
{
	FArdaRHIStatus FArdaDependencyGraph::SetOptions(const FArdaInductorOptions& O)
	{
		if (mImpl->mbCompiling)
		{
			return GraphCompiling();
		}
		if (!IsEditing())
		{
			return GraphNotEditing();
		}
		if (auto Status = ValidateArdaInductorRules(O.mRules); !Status)
		{
			return Status;
		}
		if (!O.mMinimumAsyncChain || !O.mMaxSearchStates || !O.mFramesInFlight || !O.mGpuTimingSampleInterval ||
		    !O.mAdaptiveSchedulingInterval || !O.mAdaptiveSchedulingMinSamples || !O.mAdaptiveSchedulingSearchBudget)
		{
			return GraphInvalid("Inductor chain, search and frame-slot limits must be positive.");
		}
		if (O.mbEnableAdaptiveScheduling && !AllowsArdaInductorTimingOptimization(O))
		{
			return GraphInvalid(
			    "Adaptive scheduling requires every selected graph rule to permit timing-based reordering.");
		}
		if (!std::isfinite(O.mGpuTimingEmaAlpha) || O.mGpuTimingEmaAlpha <= 0 || O.mGpuTimingEmaAlpha > 1 ||
		    !std::isfinite(O.mAdaptiveSchedulingMinImprovement) || O.mAdaptiveSchedulingMinImprovement < 0 ||
		    O.mAdaptiveSchedulingMinImprovement >= 1 || (O.mbEnableAdaptiveScheduling && !O.mbEnableGpuTiming))
		{
			return GraphInvalid(
			    "Telemetry requires EMA alpha in (0,1]; adaptive scheduling requires telemetry and an improvement threshold in [0,1).");
		}
		if (O.mSearchMode > EArdaInductorSearchMode::Exhaustive || O.mObjective > EArdaInductorObjective::Memory ||
		    O.mCudaGraphMode > EArdaCudaGraphMode::Require)
		{
			return GraphInvalid("Invalid Inductor scheduling or capture mode.");
		}
		mImpl->mOptions = O;
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::AddDependency(FArdaGraphNodeHandle P, FArdaGraphNodeHandle C)
	{
		auto& G = *mImpl;
		if (G.mbCompiling)
		{
			return GraphCompiling();
		}
		if (!G.mbEditing)
		{
			return GraphNotEditing();
		}
		if (P == C || !G.mTopology.ContainsNode(P) || !G.mTopology.ContainsNode(C))
		{
			return GraphInvalid("Manual dependencies require distinct nodes from this graph.");
		}
		for (const auto& E : G.mManualEdges)
		{
			if (E.first == P && E.second == C)
			{
				return {};
			}
		}
		G.mManualEdges.push_back({P, C});
		(void)G.mTopology.AddEdge(P, C, {});
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::RemoveDependency(FArdaGraphNodeHandle P, FArdaGraphNodeHandle C)
	{
		auto& G = *mImpl;
		if (G.mbCompiling)
		{
			return GraphCompiling();
		}
		if (!G.mbEditing)
		{
			return GraphNotEditing();
		}
		auto I = eastl::find(G.mManualEdges.begin(), G.mManualEdges.end(), eastl::make_pair(P, C));
		if (I == G.mManualEdges.end())
		{
			return GraphInvalid("No explicit dependency connects these nodes.");
		}
		// Edits start from authored declarations, before preprocessing adds hazards. Recompute
		// this endpoint's resource constraint so removing an explicit edge never erases it.
		auto Dependencies = G.mTopology.CloneSnapshot();
		const auto Edges = Dependencies.GetEdges();
		for (auto Edge : Edges)
		{
			Dependencies.RemoveEdge(Edge);
		}
		(void)ResolveArdaDependencyResourceEdges(G, Dependencies, false);
		G.mManualEdges.erase(I);
		if (auto E = G.mTopology.FindEdge(P, C))
		{
			G.mTopology.RemoveEdge(E);
		}
		if (auto E = Dependencies.FindEdge(P, C))
		{
			(void)G.mTopology.AddEdge(P, C, Dependencies.TryGetEdge(E)->mPayload);
		}
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::RemoveNode(FArdaGraphNodeHandle H)
	{
		auto& G = *mImpl;
		if (G.mbCompiling)
		{
			return GraphCompiling();
		}
		if (!G.mbEditing)
		{
			return GraphNotEditing();
		}
		if (!G.mTopology.ContainsNode(H))
		{
			return GraphInvalid("Unknown node.");
		}
		// Resolve the current edit, including uncompiled nodes, before removing any writer.
		// Overwrites and preceding readers constrain execution but do not consume the removed value.
		auto Dependencies = G.mTopology.CloneSnapshot();
		const auto Edges = Dependencies.GetEdges();
		for (auto Edge : Edges)
		{
			Dependencies.RemoveEdge(Edge);
		}
		(void)ResolveArdaDependencyResourceEdges(G, Dependencies, false);
		eastl::vector<FArdaGraphNodeHandle> Removed{H};
		eastl::unordered_set<uint32_t> Resources;
		eastl::unordered_set<uint32_t> RemovedIndices{H.GetIndex()};
		for (size_t Index = 0; Index < Removed.size(); ++Index)
		{
			for (const auto& Access : G.mTopology.TryGetNode(Removed[Index])->mPayload.mDesc.mAccesses)
			{
				if (Access.mAccess != EArdaDependencyAccess::Read && G.HasResource(Access.mResource))
				{
					Resources.insert(Access.mResource.mIndex);
				}
			}
			for (const auto Edge : Dependencies.GetOutgoingEdges(Removed[Index]))
			{
				const auto& Dependency = *Dependencies.TryGetEdge(Edge);
				if (Dependency.mPayload.mbResource && RemovedIndices.insert(Dependency.mTo.GetIndex()).second)
				{
					Removed.push_back(Dependency.mTo);
				}
			}
		}
		for (auto N : Removed)
		{
			G.mNames.erase(G.mTopology.TryGetNode(N)->mPayload.mName);
			G.mTopology.RemoveNode(N);
		}
		G.mManualEdges.erase(eastl::remove_if(G.mManualEdges.begin(),
		                         G.mManualEdges.end(),
		                         [&](auto E)
		                         {
			                         return !G.mTopology.ContainsNode(E.first) || !G.mTopology.ContainsNode(E.second);
		                         }),
		    G.mManualEdges.end());
		for (auto R : Resources)
		{
			// Preserve output roots still produced by an independent surviving range writer.
			bool Produced = false;
			for (auto N : G.mTopology.GetNodes())
			{
				for (const auto& A : G.mTopology.TryGetNode(N)->mPayload.mDesc.mAccesses)
				{
					Produced |= A.mResource.mIndex == R && A.mAccess != EArdaDependencyAccess::Read;
				}
			}
			if (!Produced)
			{
				G.mResources[R].mbOutput = false;
			}
		}
		return {};
	}
}
