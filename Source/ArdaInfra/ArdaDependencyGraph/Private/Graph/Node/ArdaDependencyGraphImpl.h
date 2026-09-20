#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"
#include "Inductor/Runtime/ArdaInductorRuntime.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"
#include "Inductor/Rules/ArdaInductorBindingRules.h"
#include "Inductor/Rules/ArdaInductorRequirements.h"
#include "Inductor/Rules/ArdaInductorAdaptiveSchedule.h"

namespace arda
{
	struct FArdaDependencyGraph::FArdaImpl
	{
		/** Intern compiler-generated layouts so equal patterns share native PSO cache keys. */
		eastl::vector<FArdaRHIBindingLayoutRef> mAutomaticLayouts;
		FArdaRHIDeviceRef mDevice;
		uint64_t mIdentity = 0;
		uint64_t mNextFrameSequence = 1;
		bool mbEditing = false;
		/** Prevent captured-graph mutation and recursive compilation from extension callbacks. */
		bool mbCompiling = false;
		bool mbExecuting = false;
		uint64_t mNextResourceGeneration = 1;
		uint64_t mNextAttachmentOrder = 1;
		/** Monotonic facade identities and scope ownership; intentionally not restored on cancellation. */
		uint64_t mNextOperationName = 1;
		uint64_t mEditGeneration = 0;
		eastl::vector<uint64_t> mResourceGenerations, mSavedResourceGenerations;
		/** Current construction handles accepted after selecting an equivalent persistent revision. */
		eastl::vector<eastl::vector<uint64_t>> mResourceAliases;
		/** Aliases retained by immutable node parameters; authoring alias transfers cannot invalidate them. */
		eastl::vector<eastl::vector<uint64_t>> mCompiledResourceAliases;
		eastl::vector<FArdaRHIBufferRef> mPersistentBuffers;
		eastl::vector<FArdaRHITextureRef> mPersistentTextures;
		FArdaInductorOptions mOptions;
		eastl::shared_ptr<FArdaInductorTimingAccumulator> mTiming =
		    eastl::make_shared<FArdaInductorTimingAccumulator>();
		eastl::unordered_map<eastl::string, double> mCostOverrides;
		eastl::unordered_map<eastl::string, eastl::string> mCostOverrideKeys;
		eastl::unordered_map<eastl::string, eastl::weak_ptr<const FArdaDependencyNodeExecutable>>
		    mCostOverrideDefinitions;
		FArdaDependencyTopology mTopology;
		eastl::vector<FArdaDependencyResourceDesc> mResources;
		eastl::unordered_map<eastl::string, FArdaGraphNodeHandle> mNames;
		eastl::unordered_map<eastl::string, uint32_t> mResourceNames;
		eastl::vector<eastl::pair<FArdaGraphNodeHandle, FArdaGraphNodeHandle>> mManualEdges;
		FArdaInductorCompileResult mCompile;
		/** Original construction, before preprocess transformations, for forced recompilation and extension. */
		eastl::shared_ptr<const FArdaImpl> mCompilerSource;
		eastl::unique_ptr<FArdaInductorRuntime> mRuntime;
		/** Compiled logical allocation plan, materialized only by the first submission. */
		eastl::unique_ptr<FArdaMemoryPlan> mPendingMemoryPlan;
		/** Immutable native PSOs are shared across this graph's persistent revisions. */
		eastl::shared_ptr<FArdaPipelineStateCache> mPipelineCache;
		eastl::shared_ptr<FArdaInductorAdaptiveJob> mAdaptiveJob;
		FArdaInductorAdaptiveScheduleState mAdaptiveSearch;
		FArdaInductorAdaptiveSchedulingStats mAdaptiveStats;
		uint64_t mLastAdaptiveSequence = 0;
		eastl::unique_ptr<FArdaDependencyTopology> mSavedTopology;
		eastl::vector<FArdaDependencyResourceDesc> mSavedResources;
		eastl::vector<eastl::pair<FArdaGraphNodeHandle, FArdaGraphNodeHandle>> mSavedManualEdges;
		FArdaInductorOptions mSavedOptions;

		bool HasResource(FArdaDependencyResourceHandle H) const
		{
			const auto HasAlias = [&](const auto& Aliases)
			{
				return H.mIndex < Aliases.size() &&
				    eastl::find(Aliases[H.mIndex].begin(), Aliases[H.mIndex].end(), H.mGeneration) !=
				    Aliases[H.mIndex].end();
			};
			return H.mGraph == mIdentity && H.mIndex < mResources.size() &&
			    (H.mGeneration == mResourceGenerations[H.mIndex] || HasAlias(mResourceAliases) ||
			        HasAlias(mCompiledResourceAliases));
		}
	};

}
