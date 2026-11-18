#pragma once
#include "Inductor/Rules/ArdaInductorGraphInternal.h"
#include "Inductor/Compiler/ArdaInductorPipelineCompiler.h"
#include "Inductor/Rules/ArdaInductorRules.h"
#include <EASTL/unique_ptr.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_set.h>
#include <EASTL/numeric_limits.h>

namespace arda::inductor_detail
{
	double NodeCost(const FArdaDependencyGraph::FArdaImpl&, const FArdaDependencyNode&);
	FArdaRHIStatus AssignQueues(const FArdaDependencyGraph::FArdaImpl&, FArdaInductorCompileResult&);

	/** Candidate state shared by the independent memory, ordering and evaluation rules. */
	struct FArdaScheduleSearch
	{
		FArdaDependencyGraph::FArdaImpl& mGraph;
		eastl::vector<FArdaHandle> mNodes;
		eastl::vector<FArdaMemoryRequest> mRequests;
		eastl::unordered_map<uint32_t, size_t> mIndices;
		eastl::vector<eastl::vector<size_t>> mPred, mNext, mUses;
		eastl::vector<double> mCritical;
		EArdaInductorObjective mObjective;
		EArdaInductorSearchMode mSearchMode;
		uint32_t mMaxSearchStates;
		uint64_t mWorkspace = 0;
		uint64_t mBestBytes = UINT64_MAX;
		double mBestCost = eastl::numeric_limits<double>::infinity();
		eastl::vector<FArdaHandle> mBestOrder;
		FArdaMemoryPlan mBestPlan;
		FArdaInductorCompileResult& mResult;
		uint64_t mStates = 0;
		bool mbComplete = false;
		bool mbExhausted = false;
		bool mbHasBestPlan = false;
		FArdaRHIStatus mLastPlanFailure;

		FArdaScheduleSearch(FArdaDependencyGraph::FArdaImpl& Graph,
		    eastl::vector<FArdaHandle> Nodes,
		    FArdaInductorCompileResult& Result)
		    : mGraph(Graph),
		      mNodes(eastl::move(Nodes)),
		      mResult(Result),
		      mObjective(Graph.mOptions.mObjective),
		      mSearchMode(Graph.mOptions.mSearchMode),
		      mMaxSearchStates(Graph.mOptions.mMaxSearchStates)
		{
		}

		FArdaRHIStatus Initialize();
		void Evaluate(const eastl::vector<size_t>& Order, bool Serialize);
		void Greedy(bool Memory);
		void ImproveIncumbent();
		void Explore();
	};

	FArdaRHIStatus ValidateNodeOrder(const FArdaDependencyGraph::FArdaImpl&, const FArdaInductorCompileResult&);
	FArdaRHIStatus ValidateCompiledResult(const FArdaDependencyGraph::FArdaImpl&, const FArdaInductorCompileResult&);
	FArdaRHIStatus PublishSchedule(FArdaScheduleSearch&, FArdaInductorCompileResult&);
	FArdaRHIStatus ValidatePipelines(const FArdaDependencyGraph::FArdaImpl&, const eastl::vector<FArdaHandle>&);
	FArdaRHIStatus ValidateQueueAssignment(const FArdaInductorQueueContext&, const FArdaInductorCompileResult&);
	void BuildCudaBatches(const FArdaDependencyGraph::FArdaImpl&, FArdaInductorCompileResult&);
}
