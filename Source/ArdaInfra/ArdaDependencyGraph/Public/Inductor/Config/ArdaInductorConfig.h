/** Schedule search, resource budgets and execution telemetry configuration. */
#pragma once

#include "RHI/Scheduling/ArdaCudaGraph.h"
#include "Inductor/Rules/ArdaInductorRules.h"
#include <cstdint>

namespace arda
{
	/** Scheduling objective. Both modes always preserve declared dependencies. */
	enum class EArdaInductorObjective : uint8_t
	{
		Efficiency,
		Memory
	};
	/** Bounded search retains its best feasible plan; exhaustive search has no state limit. */
	enum class EArdaInductorSearchMode : uint8_t
	{
		Greedy,
		Bounded,
		Exhaustive
	};

	/** Controls deterministic schedule search and the physical graph allocation budget. */
	struct FArdaInductorOptions
	{
		/** Explicit compiler rule program, configured during graph setup and immutable while compiling. */
		FArdaInductorRuleSet mRules;
		EArdaInductorObjective mObjective = EArdaInductorObjective::Efficiency;
		EArdaInductorSearchMode mSearchMode = EArdaInductorSearchMode::Bounded;
		/** Zero means no hard cap. Includes imports once, graph allocations and declared adapter workspaces. */
		uint64_t mMaxVramBytes = 0;
		uint32_t mMinimumAsyncChain = 4;
		uint32_t mMinimumAsyncSlack = 4;
		/** Work budget for expanded search prefixes and whole-order local candidates. */
		uint32_t mMaxSearchStates = 10000;
		/** Independent transient pools. Imports, persistent resources and retained adapter storage are shared. */
		uint32_t mFramesInFlight = 1;
		EArdaCudaGraphMode mCudaGraphMode = EArdaCudaGraphMode::Prefer;
		/** Relative cost of a graphics/CUDA boundary, in the same units as node cost hints. */
		uint32_t mCudaHandoffCost = 8;
		/** Cost of one auxiliary submission needed to activate or retire an aliased image. */
		uint32_t mAliasSubmissionCost = 1;
		/** Optional timestamp instrumentation. Collection polls only; unsupported queues omit samples. */
		bool mbEnableGpuTiming = false;
		/** Weight of each newly collected sample in the execution-time EMA, in (0, 1]. */
		double mGpuTimingEmaAlpha = 0.2;
		/** Attempt instrumentation every Nth submitted frame; pending queries can defer sampling. */
		uint32_t mGpuTimingSampleInterval = 1;
		/** Maximum raw node samples retained by this graph; zero retains only EMA statistics. */
		uint32_t mGpuTimingHistoryCapacity = 4096;
		/** Enables background schedule search using measured EMA costs; requires GPU timing. */
		bool mbEnableAdaptiveScheduling = false;
		/** Minimum submitted frames between background search iterations. */
		uint32_t mAdaptiveSchedulingInterval = 30;
		/** Positive samples required for each measured node before automatic tuning starts. */
		uint32_t mAdaptiveSchedulingMinSamples = 8;
		/** Maximum schedule candidates examined by one background iteration. */
		uint32_t mAdaptiveSchedulingSearchBudget = 128;
		/** Required modeled relative improvement before adopting a schedule, in [0, 1). */
		double mAdaptiveSchedulingMinImprovement = 0.02;
		bool mbEnableCopyQueue = true;
		bool mbEnableAsyncCompute = true;
	};
}
