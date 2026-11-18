#pragma once

#include "Inductor/Runtime/ArdaInductorRuntime.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"
#include "Inductor/Rules/ArdaInductorAdaptiveSchedule.h"
#include <atomic>

namespace arda
{
	/** A measured cost is valid only for the node definition and parameters that produced it. */
	struct FArdaInductorCostOverride
	{
		double mCost = 0;
		eastl::string mCanonicalKey;
		eastl::weak_ptr<const FArdaDependencyNodeExecutable> mDefinition;
	};

	/** Device ownership and mutable execution products belong to a compilation, never its authored topology. */
	struct FArdaInductorExecutionState
	{
		/** Mutable execution instances can be owned by one graph/executor only. */
		std::atomic<bool> mbExecutorClaimed{false};
		eastl::vector<FArdaRHIBindingLayoutRef> mAutomaticLayouts;
		FArdaRHIDeviceRef mDevice;
		uint64_t mNextFrameSequence = 1;
		eastl::vector<FArdaRHIBufferRef> mPersistentBuffers;
		eastl::vector<FArdaRHITextureRef> mPersistentTextures;
		eastl::shared_ptr<FArdaInductorTimingAccumulator> mTiming =
		    eastl::make_shared<FArdaInductorTimingAccumulator>();
		eastl::unordered_map<eastl::string, FArdaInductorCostOverride> mCostOverrides;
		eastl::unique_ptr<FArdaInductorRuntime> mRuntime;
		eastl::shared_ptr<FArdaPipelineStateCache> mPipelineCache;
		eastl::shared_ptr<FArdaInductorScheduleCandidate> mPendingSchedule;
		FArdaInductorAdaptiveScheduleState mAdaptiveSearch;
		FArdaInductorAdaptiveSchedulingStats mAdaptiveStats;
		uint64_t mLastAdaptiveSequence = 0;
	};
}
