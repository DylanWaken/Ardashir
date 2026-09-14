#pragma once
#include "ArdaDependencyGraph.h"
#include "Allocator/ArdaMemoryPlanner.h"
#include "ArdaInductorCommandProgram.h"
#include "ArdaInductorAdaptiveSchedule.h"

#include <future>
#include <atomic>

namespace arda
{
	/** The native callback is the sole promise owner after registration. Discarding
	 * an unsubmitted list breaks that promise and cancels its wait automatically.
	 * Completion threads own bytes only; publishing caller destinations is deferred.
	 */
	class FArdaInductorReadbackCompletions
	{
	public:
		[[nodiscard]] FArdaRHIDeviceToHostCopyCallback Register(eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
		    uint32_t PassIndex);
		/** Waits every registered callback, including submitted callbacks after a partial
		 * failure, then publishes all outputs or clears them. Called after recording
		 * storage has released every command list that did not reach submission.
		 */
		[[nodiscard]] FArdaRHIStatus Drain(FArdaRHIStatus SubmissionStatus);
		[[nodiscard]] bool IsReady();

	private:
		struct FArdaPending
		{
			std::future<FArdaRHIBufferReadbackResult> mFuture;
			eastl::shared_ptr<eastl::vector<uint8_t>> mDestination;
			uint32_t mPassIndex = 0;
		};

		std::mutex mMutex;
		eastl::vector<FArdaPending> mPending;
	};

	class FArdaInductorTimingAccumulator
	{
	public:
		void Configure(double Alpha, uint32_t HistoryCapacity);
		void Add(const eastl::string& Name,
		    double Seconds,
		    const eastl::vector<FArdaGraphNodeHandle>& Nodes = {},
		    uint64_t Sequence = 0,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics,
		    uint64_t Epoch = UINT64_MAX);
		eastl::vector<FArdaInductorTimingSample> Snapshot() const;
		eastl::vector<FArdaInductorNodeTiming> History() const;
		uint64_t GetEpoch() const;
		void Clear();

	private:
		mutable std::mutex mMutex;
		eastl::unordered_map<eastl::string, FArdaInductorTimingSample> mSamples;
		eastl::vector<FArdaInductorNodeTiming> mHistory;
		double mAlpha = 0.2;
		uint32_t mHistoryCapacity = 4096;
		uint32_t mHistoryCursor = 0;
		uint64_t mEpoch = 0;
	};

	struct FArdaInductorFrameTimer
	{
		eastl::string mName;
		FArdaRHITimerQueryRef mQuery;
		eastl::vector<FArdaGraphNodeHandle> mNodes;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
	};

	struct FArdaInductorCudaFrameTimer
	{
		eastl::shared_ptr<FArdaCudaTimingQuery> mQuery;
		eastl::vector<FArdaGraphNodeHandle> mNodes;
		eastl::vector<eastl::string> mNames;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
	};

	/** Completion ownership is independent of a reusable frame slot and survives edits. */
	struct FArdaDependencyFrameTicket::FArdaState
	{
		FArdaRHIDeviceRef mDevice;
		uint64_t mGraphIdentity = 0;
		uint64_t mSequence = 0;
		FArdaGraphExecutionResult mExecution;
		FArdaRHIStatus mRetirementStatus;
		eastl::vector<FArdaRHIGpuFenceRef> mRecoveryFences;
		eastl::vector<FArdaInductorFrameTimer> mTimers;
		eastl::vector<FArdaInductorCudaFrameTimer> mCudaTimers;
		bool mbSampleTiming = false;
		uint64_t mTimingEpoch = 0;
		eastl::shared_ptr<FArdaInductorTimingAccumulator> mTiming;
		FArdaInductorReadbackCompletions mReadbackCompletions;
		std::mutex mMutex;
		bool mbComplete = false;
	};

	/** One independently reusable physical pool and cached native lowering. */
	struct FArdaInductorFrame
	{
		FArdaMemoryResources mMemory;
		eastl::unique_ptr<FArdaInductorCommandProgram> mLowered;
		eastl::vector<FArdaInductorBuffer*> mBuffers;
		eastl::vector<FArdaInductorTexture*> mTextures;
		eastl::vector<FArdaInductorAccelerationStructure*> mAccelerationStructures;
		eastl::unordered_map<uint32_t, eastl::unordered_map<eastl::string, FArdaInductorResolvedPipeline>> mPipelines;
		eastl::unordered_map<uint32_t, FArdaRHIFramebufferRef> mFramebuffers;
		eastl::unordered_map<uint32_t, eastl::unordered_map<eastl::string, eastl::vector<FArdaRHIBindingSetRef>>>
		    mBindings;
		eastl::unordered_map<uint32_t, eastl::unordered_map<eastl::string, FArdaRHIDescriptorTableRef>>
		    mDescriptorTables;
		eastl::unordered_map<uint32_t, eastl::unordered_map<eastl::string, FArdaRHIShaderTableRef>> mShaderTables;
		eastl::unordered_map<uint32_t, eastl::unordered_map<eastl::string, eastl::vector<uint8_t>>> mShaderParameters;
		eastl::shared_ptr<FArdaDependencyFrameTicket::FArdaState> mActive;
		/** At most one outstanding timing generation; busy queries skip subsequent instrumentation. */
		eastl::shared_ptr<FArdaDependencyFrameTicket::FArdaState> mTimingState;
		eastl::vector<FArdaInductorFrameTimer> mTimers;
		eastl::vector<FArdaInductorCudaFrameTimer> mCudaTimers;
		uint32_t mIndex = 0;
		eastl::unordered_map<uint32_t, eastl::shared_ptr<FArdaCudaGraphCache>> mCudaCaches;
	};

	/** Immutable native references used to prepare a different schedule without reallocating GPU storage. */
	struct FArdaInductorReuseFrame
	{
		FArdaMemoryResources mMemory;
		eastl::vector<EArdaRHIResourceState> mBufferStates, mTextureStates;
		eastl::unordered_map<uint32_t, eastl::unordered_map<eastl::string, FArdaInductorResolvedPipeline>> mPipelines;
		eastl::unordered_map<uint32_t, FArdaRHIFramebufferRef> mFramebuffers;
		eastl::unordered_map<uint32_t, eastl::unordered_map<eastl::string, eastl::vector<FArdaRHIBindingSetRef>>>
		    mBindings;
		eastl::unordered_map<uint32_t, eastl::unordered_map<eastl::string, FArdaRHIDescriptorTableRef>>
		    mDescriptorTables;
		eastl::unordered_map<uint32_t, eastl::unordered_map<eastl::string, FArdaRHIShaderTableRef>> mShaderTables;
		eastl::unordered_map<uint32_t, eastl::unordered_map<eastl::string, eastl::vector<uint8_t>>> mShaderParameters;
		eastl::vector<FArdaInductorFrameTimer> mTimers;
		eastl::vector<FArdaInductorCudaFrameTimer> mCudaTimers;
		eastl::unordered_map<uint32_t, eastl::shared_ptr<FArdaCudaGraphCache>> mCudaCaches;
	};

	struct FArdaInductorGraphBinding
	{
		FArdaDependencyGraph::FArdaImpl* mGraph = nullptr;
	};
	struct FArdaInductorAdaptiveJob;

	/** Device-bound slots and scheduling state, separate from semantic topology. */
	struct FArdaInductorRuntime
	{
		eastl::vector<eastl::unique_ptr<FArdaInductorFrame>> mFrames;
		eastl::shared_ptr<FArdaDependencyFrameTicket::FArdaState> mPrevious;
		uint32_t mNextSlot = 0;
		bool mbHasSharedResources = false;
		FArdaMemoryPlan mMemoryPlan;
		eastl::shared_ptr<const FArdaMemoryPlan> mAdaptivePlan;
		eastl::shared_ptr<FArdaInductorGraphBinding> mBinding;
		eastl::shared_ptr<const FArdaDependencyGraph::FArdaImpl> mAdaptiveSeed;
		eastl::shared_ptr<const eastl::vector<FArdaInductorReuseFrame>> mReuseFrames;
		~FArdaInductorRuntime();
		static FArdaRHIStatus Prepare(FArdaDependencyGraph::FArdaImpl& Graph,
		    FArdaMemoryPlan Plan,
		    const eastl::vector<FArdaInductorReuseFrame>* Reuse = nullptr);
		static void CollectTelemetry(FArdaDependencyGraph::FArdaImpl& Graph);
		static void CollectStateTelemetry(FArdaDependencyFrameTicket::FArdaState& State);
		static bool TryRetireAll(FArdaDependencyGraph::FArdaImpl& Graph);
		static TArdaRHIResult<FArdaDependencyFrameTicket> Submit(FArdaDependencyGraph::FArdaImpl& Graph,
		    const FArdaGraphExecuteOptions& Options);
		static FArdaGraphExecutionResult Wait(FArdaDependencyGraph::FArdaImpl& Graph,
		    const FArdaDependencyFrameTicket& Ticket);
		static TArdaRHIResult<bool> IsComplete(FArdaDependencyGraph::FArdaImpl& Graph,
		    const FArdaDependencyFrameTicket& Ticket);
		FArdaRHIStatus WaitAll();
		static FArdaGraphExecutionResult Execute(FArdaDependencyGraph::FArdaImpl& Graph,
		    const FArdaGraphExecuteOptions& Options);
		static FArdaGraphExecutionResult WaitState(FArdaDependencyFrameTicket::FArdaState& State);
	};

	struct FArdaDependencyGraph::FArdaImpl
	{
		/** Intern compiler-generated layouts so equal patterns share native PSO cache keys. */
		eastl::vector<FArdaRHIBindingLayoutRef> mAutomaticLayouts;
		FArdaRHIDeviceRef mDevice;
		uint64_t mIdentity = 0;
		uint64_t mNextFrameSequence = 1;
		bool mbEditing = false;
		bool mbExecuting = false;
		uint64_t mNextResourceGeneration = 1;
		uint64_t mNextAttachmentOrder = 1;
		eastl::vector<uint64_t> mResourceGenerations, mSavedResourceGenerations;
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
		eastl::unique_ptr<FArdaInductorRuntime> mRuntime;
		eastl::unique_ptr<FArdaPipelineStateCache> mPipelineCache;
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
			return H.mGraph == mIdentity && H.mIndex < mResources.size() &&
			    H.mGeneration == mResourceGenerations[H.mIndex];
		}
	};

	/** Copies only immutable semantic and scheduling inputs, never active frame ownership. */
	FArdaRHIStatus DescribeArdaShaderBindings(FArdaDependencyGraph::FArdaImpl& Graph, FArdaDependencyNodeDesc& Desc);
	void InferArdaNodeRequirements(const FArdaDependencyGraph::FArdaImpl& Graph,
	    EArdaDependencyNodeKind Kind,
	    FArdaDependencyNodeDesc& Desc);
	FArdaRHIStatus PrepareArdaShaderBindings(const FArdaDependencyGraph::FArdaImpl& Graph, FArdaInductorFrame& Frame);

	/** Copies only immutable semantic and scheduling inputs, never active frame ownership. */
	eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> CloneArdaInductorTimingInputs(
	    const FArdaDependencyGraph::FArdaImpl& Graph);
	void AdvanceArdaInductorTelemetry(FArdaDependencyGraph::FArdaImpl& Graph);
	FArdaRHIStatus RequestArdaInductorTimingOptimization(FArdaDependencyGraph::FArdaImpl& Graph, bool Automatic);
	void CancelArdaInductorTimingOptimization(FArdaDependencyGraph::FArdaImpl& Graph);
}
