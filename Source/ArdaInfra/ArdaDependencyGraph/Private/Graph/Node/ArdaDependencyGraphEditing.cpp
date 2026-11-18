#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaDependencyGraphStatus.h"
#include "Inductor/Compiler/ArdaInductor.h"
#include "Inductor/Runtime/ArdaInductorRuntime.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"
#include <cmath>

namespace arda
{
	FArdaRHIStatus FArdaDependencyGraph::SetOptions(const FArdaInductorOptions& O)
	{
		if (mImpl->mbCompiling || mImpl->mbPreprocessing)
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
		mImpl->mbPreprocessed = false;
		mImpl->mCompilerSource.reset();
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::AddDependency(FArdaGraphNodeHandle P, FArdaGraphNodeHandle C)
	{
		auto& G = *mImpl;
		if (G.mbCompiling || (G.mbPreprocessing && !G.mbPreprocessEditing))
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

}
