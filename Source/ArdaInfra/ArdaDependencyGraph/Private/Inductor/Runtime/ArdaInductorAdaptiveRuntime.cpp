#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphClone.h"
#include "Inductor/Runtime/ArdaInductorFrame.h"
#include "Inductor/Runtime/ArdaInductorFrameTicketState.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"
#include "Inductor/Rules/ArdaInductorAdaptiveSchedule.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cmath>

namespace arda
{
	eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> CloneArdaInductorTimingInputs(
	    const FArdaDependencyGraph::FArdaImpl& G)
	{
		auto S = CloneArdaGraphConstruction(G);
		const auto Execution = S->mCompile.mExecution;
		S->mCompile = G.mCompile;
		S->mbUsingCompiledDescriptions = true;
		// Evaluate the candidate against isolated runtime state and the current timing profile.
		S->mCompile.mExecution = Execution;
		S->mCompile.mExecution->mCostOverrides = G.mCompile.mExecution->mCostOverrides;
		S->mCompile.mExecution->mTiming = G.mCompile.mExecution->mTiming;
		return S;
	}

	struct FArdaInductorScheduleCandidate
	{
		uint64_t mRevision = 0, mEpoch = 0;
		FArdaRHIStatus mStatus;
		FArdaInductorAdaptiveScheduleState mSearch;
		FArdaInductorAdaptiveScheduleResult mResult;
		eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> mSnapshot;
	};

	bool ApplyArdaInductorTimingCosts(FArdaDependencyGraph::FArdaImpl& Graph,
	    const eastl::vector<FArdaInductorTimingSample>& Samples,
	    uint32_t MinimumSamples)
	{
		struct FArdaAttribution
		{
			FArdaGraphNodeHandle mNode;
			double mSeconds = 0;
			uint64_t mSequence = 0;
			bool mbIndividual = false;
		};

		eastl::unordered_map<uint32_t, FArdaAttribution> Attributed;
		for (const auto& Sample : Samples)
		{
			if (Sample.mNodes.empty() || Sample.mSampleCount < MinimumSamples || !std::isfinite(Sample.mGpuSeconds) ||
			    Sample.mGpuSeconds <= 0)
			{
				continue;
			}
			double TotalWeight = 0;
			eastl::vector<double> Weights;
			eastl::unordered_set<uint32_t> Seen;
			for (auto Handle : Sample.mNodes)
			{
				const auto* Node = Graph.mTopology.TryGetNode(Handle);
				const double Weight = Node ? GetArdaInductorScheduleNodeCost(Graph, Node->mPayload) : 0;
				if (!Node || !Seen.insert(Handle.GetIndex()).second || !std::isfinite(Weight) || Weight <= 0)
				{
					Weights.clear();
					break;
				}
				Weights.push_back(Weight);
				TotalWeight += Weight;
			}
			if (Weights.size() != Sample.mNodes.size() || !std::isfinite(TotalWeight) || TotalWeight <= 0)
			{
				continue;
			}
			for (size_t Index = 0; Index < Sample.mNodes.size(); ++Index)
			{
				const auto Handle = Sample.mNodes[Index];
				const bool Individual = Sample.mNodes.size() == 1;
				const auto Previous = Attributed.find(Handle.GetIndex());
				// Old grouping variants may overlap; do not double-count a node's estimated duration.
				if (Previous != Attributed.end() &&
				    (Previous->second.mSequence > Sample.mLastFrameSequence ||
				        (Previous->second.mSequence == Sample.mLastFrameSequence && Previous->second.mbIndividual)))
				{
					continue;
				}
				Attributed[Handle.GetIndex()] = {Handle,
				    Sample.mGpuSeconds * (Weights[Index] / TotalWeight),
				    Sample.mLastFrameSequence,
				    Individual};
			}
		}
		double Hints = 0, Seconds = 0;
		for (const auto& Entry : Attributed)
		{
			Hints += Graph.mCompile.mNodes.at(Entry.second.mNode).mDesc.mEstimatedCost;
			Seconds += Entry.second.mSeconds;
		}
		if (!std::isfinite(Seconds) || Seconds <= 0 || !std::isfinite(Hints) || Hints <= 0)
		{
			return false;
		}
		for (const auto& Entry : Attributed)
		{
			const auto& Node = Graph.mTopology.TryGetNode(Entry.second.mNode)->mPayload;
			Graph.mCompile.mExecution->mCostOverrides[Node.mName] = {(Entry.second.mSeconds / Seconds) * Hints,
			    Node.mCanonicalKey,
			    Node.mDefinition};
		}
		return true;
	}

	FArdaRHIStatus RequestArdaInductorTimingOptimization(FArdaDependencyGraph::FArdaImpl& G, bool Automatic)
	{
		if (!AllowsArdaInductorTimingOptimization(G.mOptions))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Timing optimization requires every selected graph rule to permit timing-based reordering.");
		}
		if (G.mbEditing || G.mbExecuting || !G.mCompile.mExecution->mRuntime ||
		    !G.mCompile.mExecution->mRuntime->mAdaptiveSeed)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Timing optimization requires a compiled graph with telemetry enabled.");
		}
		if (G.mCompile.mExecution->mPendingSchedule)
		{
			return {};
		}
		const auto Profile = G.mCompile.mExecution->mTiming->Snapshot();
		const auto Required = Automatic ? G.mOptions.mAdaptiveSchedulingMinSamples : 1u;
		bool HasSamples = false;
		for (const auto& P : Profile)
		{
			HasSamples |=
			    !P.mNodes.empty() && P.mSampleCount >= Required && std::isfinite(P.mGpuSeconds) && P.mGpuSeconds > 0;
		}
		if (!HasSamples)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "No eligible positive node or pipeline-batch GPU samples are available.");
		}
		auto& Execution = *G.mCompile.mExecution;
		auto Candidate = eastl::make_shared<FArdaInductorScheduleCandidate>();
		Candidate->mRevision = G.mCompile.mRevision;
		Candidate->mEpoch = Execution.mTiming->GetEpoch();
		Candidate->mSearch = Execution.mAdaptiveSearch;
		try
		{
			Candidate->mSnapshot = CloneArdaInductorTimingInputs(*Execution.mRuntime->mAdaptiveSeed);
			auto& Snapshot = *Candidate->mSnapshot;
			ApplyArdaInductorTimingCosts(Snapshot, Profile, Required);
			auto Proposal = TuneArdaInductorSchedule(Snapshot,
			    *Execution.mRuntime->mAdaptivePlan,
			    Candidate->mSearch,
			    G.mOptions.mAdaptiveSchedulingSearchBudget,
			    G.mOptions.mAdaptiveSchedulingMinImprovement);
			Candidate->mStatus = Proposal.mStatus;
			if (Proposal)
			{
				Candidate->mResult = eastl::move(Proposal.mValue);
				if (Candidate->mResult.mbImproved)
				{
					const auto CandidateExecution = Snapshot.mCompile.mExecution;
					Snapshot.mCompile = Candidate->mResult.mCompile;
					Snapshot.mCompile.mExecution = CandidateExecution;
					++Snapshot.mCompile.mRevision;
					Candidate->mStatus = FArdaInductorRuntime::Prepare(Snapshot,
					    Candidate->mResult.mMemoryPlan,
					    Execution.mRuntime->mReuseFrames.get());
				}
			}
		}
		catch (const std::exception& Error)
		{
			Candidate->mStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
		}
		catch (...)
		{
			Candidate->mStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Schedule preparation failed.");
		}
		Execution.mPendingSchedule = eastl::move(Candidate);
		Execution.mLastAdaptiveSequence = Execution.mNextFrameSequence;
		return {};
	}

	void AdvanceArdaInductorTelemetry(FArdaDependencyGraph::FArdaImpl& G)
	{
		if (G.mbEditing || G.mbExecuting || !G.mOptions.mbEnableGpuTiming)
		{
			return;
		}
		FArdaInductorRuntime::CollectTelemetry(G);
		if (G.mCompile.mExecution->mPendingSchedule)
		{
			const auto Candidate = G.mCompile.mExecution->mPendingSchedule;
			const bool Valid = Candidate->mRevision == G.mCompile.mRevision &&
			    Candidate->mEpoch == G.mCompile.mExecution->mTiming->GetEpoch();
			if (Valid && Candidate->mStatus && Candidate->mResult.mbImproved && !FArdaInductorRuntime::TryRetireAll(G))
			{
				return;
			}
			++G.mCompile.mExecution->mAdaptiveStats.mIterations;
			G.mCompile.mExecution->mAdaptiveStats.mCandidatesExamined += Candidate->mResult.mCandidatesExamined;
			G.mCompile.mExecution->mAdaptiveStats.mLastBaselineCost = Candidate->mResult.mIncumbentCost;
			G.mCompile.mExecution->mAdaptiveStats.mLastCandidateCost = Candidate->mResult.mCandidateCost;
			G.mCompile.mExecution->mAdaptiveStats.mLastStatus = Candidate->mStatus;
			if (Valid && Candidate->mStatus)
			{
				G.mCompile.mExecution->mAdaptiveSearch = Candidate->mSearch;
				if (Candidate->mResult.mbImproved && Candidate->mSnapshot->mCompile.mExecution->mRuntime)
				{
					auto& S = *Candidate->mSnapshot;
					const auto CurrentExecution = G.mCompile.mExecution;
					const auto ProposedExecution = S.mCompile.mExecution;
					S.mCompile.mExecution->mRuntime->mBinding->mGraph = &G;
					G.mCompile.mExecution->mRuntime = eastl::move(S.mCompile.mExecution->mRuntime);
					G.mCompile = eastl::move(S.mCompile);
					G.mCompile.mExecution = CurrentExecution;
					CurrentExecution->mCostOverrides = eastl::move(ProposedExecution->mCostOverrides);
					++G.mCompile.mExecution->mAdaptiveStats.mAcceptedSchedules;
				}
			}
			G.mCompile.mExecution->mPendingSchedule.reset();
		}
		if (G.mOptions.mbEnableAdaptiveScheduling && AllowsArdaInductorTimingOptimization(G.mOptions) &&
		    !G.mCompile.mExecution->mPendingSchedule &&
		    G.mCompile.mExecution->mNextFrameSequence - G.mCompile.mExecution->mLastAdaptiveSequence >=
		        G.mOptions.mAdaptiveSchedulingInterval)
		{
			(void)RequestArdaInductorTimingOptimization(G, true);
		}
	}

	void CancelArdaInductorTimingOptimization(FArdaDependencyGraph::FArdaImpl& G)
	{
		G.mCompile.mExecution->mPendingSchedule.reset();
	}

	FArdaRHIStatus FArdaDependencyGraph::OptimizeFromTimingProfile()
	{
		FArdaInductorRuntime::CollectTelemetry(*mImpl);
		return RequestArdaInductorTimingOptimization(*mImpl, false);
	}
}
