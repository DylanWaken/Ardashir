#pragma once

#include "ArdaDependencyGraph.h"
#include "ArdaInductorMemory.h"

namespace arda
{
	/** CPU-only search cursor. One worker owns it; carry it between timing-profile iterations. */
	struct FArdaInductorAdaptiveScheduleState
	{
		uint64_t mGraphIdentity = 0;
		uint64_t mRevision = 0;
		uint64_t mIteration = 0;
		uint64_t mCandidateCursor = 0;
		uint64_t mCandidatesExamined = 0;
		eastl::vector<FArdaGraphNodeHandle> mIncumbentOrder;
		eastl::vector<FArdaGraphNodeHandle> mSeedOrder;
		eastl::vector<FArdaGraphNodeHandle> mBestOrder;
	};

	/** Proposal only: the caller checks snapshot revision and adopts it on a reusable frame slot. */
	struct FArdaInductorAdaptiveScheduleResult
	{
		FArdaInductorCompileResult mCompile;
		/** Identical allocations, slots, heaps, offsets and alias edges; updated use positions only. */
		FArdaInductorMemoryPlan mMemoryPlan;
		uint32_t mCandidatesExamined = 0;
		double mIncumbentCost = 0;
		double mCandidateCost = 0;
		bool mbImproved = false;
		/** One insertion neighborhood was completed; this does not certify a global optimum. */
		bool mbNeighborhoodComplete = false;
	};

	/**
	 * Incrementally explores legal node insertions and automatic queue assignments using frozen EMA
	 * overrides. Snapshot is worker-owned and immutable, including topology/resources/options/compile
	 * and cost-override maps. Its runtime is never accessed. CandidateBudget bounds attempted orders,
	 * not wall time; each attempt is polynomial in the snapshot size. A zero budget performs no search.
	 * MinimumRelativeImprovement is finite in [0,1); only a strictly faster modeled proposal reaching
	 * that threshold is published. No GPU work, native allocations, materialization or waits occur.
	 * The compile revision is preserved so the caller can reject a stale proposal before adoption.
	 */
	[[nodiscard]] TArdaRHIResult<FArdaInductorAdaptiveScheduleResult> TuneArdaInductorSchedule(
	    const FArdaDependencyGraph::FImpl& Snapshot,
	    const FArdaInductorMemoryPlan& FixedPlan,
	    FArdaInductorAdaptiveScheduleState& State,
	    uint32_t CandidateBudget,
	    double MinimumRelativeImprovement);

	/** Compiler-shared cost model. Orders must preserve both semantic and fixed memory dependencies. */
	[[nodiscard]] TArdaRHIResult<FArdaInductorCompileResult> EvaluateArdaInductorFixedSchedule(
	    const FArdaDependencyGraph::FImpl& Snapshot,
	    const FArdaInductorMemoryPlan& FixedPlan,
	    const eastl::vector<FArdaGraphNodeHandle>& Order,
	    bool AssignAutomaticQueues);

	/** Shared hint/EMA selection, including frozen definition/key identity checks. */
	[[nodiscard]] double GetArdaInductorScheduleNodeCost(const FArdaDependencyGraph::FImpl& Snapshot,
	    const FArdaDependencyNode& Node);
}
