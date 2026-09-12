#pragma once

#include "ArdaGraph.h"
#include "ArdaDependencyGraphExecution.h"
#include "Compute/ArdaCudaSequence.h"
#include "ArdaInductorPipeline.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <mutex>
#include <type_traits>

namespace arda
{
	/** A graph-owned logical resource version. Each version has at most one producer. */
	struct FArdaDependencyResourceHandle
	{
		uint64_t mGraph = 0;
		uint32_t mIndex = UINT32_MAX;
		uint64_t mGeneration = 0;

		explicit operator bool() const noexcept
		{
			return mGraph && mIndex != UINT32_MAX;
		}

		bool operator==(const FArdaDependencyResourceHandle& Other) const noexcept
		{
			return mGraph == Other.mGraph && mIndex == Other.mIndex && mGeneration == Other.mGeneration;
		}

		bool operator!=(const FArdaDependencyResourceHandle& Other) const noexcept
		{
			return !(*this == Other);
		}
	};

	/** Execution domain; CUDA is kept on one framework stream per compiled batch. */
	enum class EArdaDependencyNodeKind : uint8_t
	{
		Graphics,
		Compute,
		Copy,
		Cuda,
		Synchronization
	};
	/** Resource values are immutable between producers. In-place external updates use ReadWrite. */
	enum class EArdaDependencyAccess : uint8_t
	{
		Read,
		Write,
		ReadWrite
	};
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

	/** One dependency derived from a node's typed parameters. */
	struct FArdaDependencyAccess
	{
		FArdaDependencyResourceHandle mResource;
		EArdaDependencyAccess mAccess = EArdaDependencyAccess::Read;
		EArdaRHIResourceState mState = EArdaRHIResourceState::ShaderResource;
		FArdaRHIBufferRange mBufferRange;
		FArdaRHITextureSubresourceRange mTextureRange;
	};

	/** Frozen resource description. External storage is retained by the graph. */
	struct FArdaDependencyResourceDesc
	{
		eastl::string mName;
		bool mbTexture = false;
		bool mbPersistent = false;
		bool mbOutput = false;
		FArdaRHIBufferDesc mBuffer;
		FArdaRHITextureDesc mTexture;
		FArdaRHIBufferRef mExternalBuffer;
		FArdaRHITextureRef mExternalTexture;
		/** Imported built or unbuilt acceleration structure; accesses cover the whole object. */
		FArdaRHIAccelStructRef mExternalAccelerationStructure;
	};

	/** Compile-time declaration returned by a registered node's parameter visitor. */
	struct FArdaDependencyNodeDesc
	{
		eastl::vector<FArdaDependencyAccess> mAccesses;
		eastl::vector<FArdaInductorPipelineContribution> mPipelineStages;
		eastl::vector<FArdaInductorPipelineRequest> mPipelines;
		eastl::vector<FArdaDependencyResourceHandle> mColorTargets;
		FArdaDependencyResourceHandle mDepthTarget;
		/** Relative cost hint used for critical path and async overlap estimates. Must be positive. */
		uint32_t mEstimatedCost = 1;
		/** Extra device memory retained by an adapter outside declared graph resources. */
		uint64_t mWorkspaceBytes = 0;
		/** Graph-owned temporary buffer, valid only during this node's work and eligible for aliasing. */
		uint64_t mTransientWorkspaceBytes = 0;
		/** Keep observable operations even when none of their resource values is consumed. */
		bool mbSideEffect = false;
		/** Pure stage declarations contribute a shader without recording a separate GPU dispatch. */
		bool mbPipelineStageOnly = false;
	};

	class FArdaInductorPassContext;
	class FArdaDependencyExecutionContext;
	class FArdaDependencyNodeBase;

	/** Immutable registered implementation. Canonical keys must cover every behavior-affecting value. */
	struct FArdaDependencyNodeDefinition
	{
		eastl::string mName;
		uint32_t mVersion = 1;
		EArdaDependencyNodeKind mKind = EArdaDependencyNodeKind::Compute;
		/** Public attachment schema used for input type validation and canonical identity, before preparation.
		 * Typed registration assigns ArdaDependencyParameterType<Parameters>; execution can use a different schema.
		 */
		const void* mParameterType = nullptr;
		/** Schema after optional preparation; defaults to the attachment schema for callback-only definitions. */
		const void* mPreparedParameterType = nullptr;
		/** Attachment-time preparation. Canonical keys consume original parameters; other callbacks consume
		 * the retained prepared result. No GPU submission is performed by the graph during preparation.
		 */
		eastl::function<TArdaRHIResult<eastl::shared_ptr<const void>>(FArdaRHIDeviceRef, eastl::shared_ptr<const void>)>
		    mPrepare;
		eastl::function<eastl::string(const void*)> mCanonicalKey;
		eastl::function<FArdaDependencyNodeDesc(const void*)> mDescribe;
		eastl::function<FArdaRHIStatus(FArdaDependencyExecutionContext&, const void*)> mRecord;
		eastl::function<FArdaRHIStatus(FArdaDependencyExecutionContext&, const void*, FArdaCudaSequence&)> mPrepareCuda;
	};

	/** Type-safe node registration. No CUDA SDK or graphics-native types are needed. */
	template <class Parameters, class PreparedParameters = Parameters>
	struct TArdaDependencyNodeDefinition
	{
		eastl::string mName;
		uint32_t mVersion = 1;
		EArdaDependencyNodeKind mKind = EArdaDependencyNodeKind::Compute;
		/** Optional when both schemas match; otherwise required. Runs only for a new instance inside an edit. */
		eastl::function<TArdaRHIResult<eastl::shared_ptr<const PreparedParameters>>(FArdaRHIDeviceRef,
		    eastl::shared_ptr<const Parameters>)>
		    mPrepare;
		eastl::function<eastl::string(const Parameters&)> mCanonicalKey;
		eastl::function<FArdaDependencyNodeDesc(const PreparedParameters&)> mDescribe;
		eastl::function<FArdaRHIStatus(FArdaDependencyExecutionContext&, const PreparedParameters&)> mRecord;
		eastl::function<FArdaRHIStatus(FArdaDependencyExecutionContext&, const PreparedParameters&, FArdaCudaSequence&)>
		    mPrepareCuda;
	};
	template <class T>
	inline const uint8_t ArdaDependencyParameterType = 0;

	/** Process-wide registry. Lookup returns retained immutable definitions; registration is synchronized. */
	class FArdaNodeRegistry final
	{
	public:
		static FArdaNodeRegistry& Get();
		FArdaRHIStatus Register(FArdaDependencyNodeDefinition Definition);
		/** Removes library lookup; existing graph instances retain their immutable definition. */
		FArdaRHIStatus Unregister(const eastl::string& Name);
		eastl::shared_ptr<const FArdaDependencyNodeDefinition> Find(const eastl::string& Name) const;
		eastl::vector<eastl::string> GetNames() const;

		template <class P, class Prepared>
		FArdaRHIStatus Register(TArdaDependencyNodeDefinition<P, Prepared> Definition)
		{
			FArdaDependencyNodeDefinition D;
			D.mName = eastl::move(Definition.mName);
			D.mVersion = Definition.mVersion;
			D.mKind = Definition.mKind;
			D.mParameterType = &ArdaDependencyParameterType<P>;
			D.mPreparedParameterType = &ArdaDependencyParameterType<Prepared>;
			if (Definition.mPrepare)
			{
				D.mPrepare = [F = eastl::move(Definition.mPrepare)](FArdaRHIDeviceRef Device,
				                 eastl::shared_ptr<const void> V) -> TArdaRHIResult<eastl::shared_ptr<const void>>
				{
					auto Result = F(eastl::move(Device), eastl::static_pointer_cast<const P>(eastl::move(V)));
					return {eastl::move(Result.mValue), eastl::move(Result.mStatus)};
				};
			}
			if (Definition.mCanonicalKey)
			{
				D.mCanonicalKey = [F = eastl::move(Definition.mCanonicalKey)](const void* V)
				{
					return F(*static_cast<const P*>(V));
				};
			}
			if (Definition.mDescribe)
			{
				D.mDescribe = [F = eastl::move(Definition.mDescribe)](const void* V)
				{
					return F(*static_cast<const Prepared*>(V));
				};
			}
			if (Definition.mRecord)
			{
				D.mRecord = [F = eastl::move(Definition.mRecord)](FArdaDependencyExecutionContext& C, const void* V)
				{
					return F(C, *static_cast<const Prepared*>(V));
				};
			}
			if (Definition.mPrepareCuda)
			{
				D.mPrepareCuda = [F = eastl::move(Definition.mPrepareCuda)](FArdaDependencyExecutionContext& C,
				                     const void* V,
				                     FArdaCudaSequence& S)
				{
					return F(C, *static_cast<const Prepared*>(V), S);
				};
			}
			return Register(eastl::move(D));
		}

	private:
		mutable std::mutex mMutex;
		eastl::unordered_map<eastl::string, eastl::shared_ptr<const FArdaDependencyNodeDefinition>> mDefinitions;
	};

	/** Controls deterministic schedule search and the physical graph allocation budget. */
	struct FArdaInductorOptions
	{
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

	/** Immutable schedule products, replaced by successful edit compilation or adaptive schedule adoption. */
	struct FArdaInductorCompileResult
	{
		FArdaRHIStatus mStatus;
		uint64_t mRevision = 0;
		eastl::vector<FArdaGraphNodeHandle> mExecutionOrder;
		eastl::vector<FArdaGraphNodeHandle> mCulledNodes;
		eastl::vector<EArdaRHIQueueType> mQueues;
		eastl::vector<eastl::vector<FArdaGraphNodeHandle>> mCudaBatches;
		eastl::vector<eastl::pair<FArdaGraphNodeHandle, FArdaGraphNodeHandle>> mMemoryDependencies;
		uint64_t mAllocatedBytes = 0;
		uint64_t mPeakLiveBytes = 0;
		uint64_t mAliasedBytes = 0;
		uint32_t mSchedulesExamined = 0;
		uint64_t mSearchStatesExamined = 0;
		/** True only if every legal topological order was evaluated by the current allocator/cost model. */
		bool mbSearchComplete = false;
		bool mbSearchExhausted = false;
		double mEstimatedExecutionCost = 0;
		eastl::unordered_map<uint32_t, uint32_t> mWorkspaceResourceIds;
	};

	/** Measured node statistics. Every collected execution contributes once, including late frame receipts. */
	struct FArdaInductorTimingSample
	{
		eastl::string mNodeName;
		/** Stable identities; display names are never used as batch or node keys. */
		eastl::vector<FArdaGraphNodeHandle> mNodes;
		/** EMA in collection order, which can differ from frame submission order. */
		double mGpuSeconds = 0;
		uint32_t mSampleCount = 0;
		/** Duration from the highest collected frame sequence; late receipts do not replace it. */
		double mLastGpuSeconds = 0;
		double mMinGpuSeconds = 0;
		double mMaxGpuSeconds = 0;
		/** Highest collected frame sequence, rather than the most recently collected receipt. */
		uint64_t mLastFrameSequence = 0;
		/** Queue used by the frame described by mLastFrameSequence and mLastGpuSeconds. */
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
	};

	/** One completed sampled node execution; durations exclude CPU recording and submission. */
	struct FArdaInductorNodeTiming
	{
		FArdaGraphNodeHandle mNode;
		eastl::string mNodeName;
		uint64_t mFrameSequence = 0;
		double mGpuSeconds = 0;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
	};

	/** Automatic tuning is a bounded heuristic search, not a guarantee of a global optimum. */
	struct FArdaInductorAdaptiveSchedulingStats
	{
		uint64_t mIterations = 0;
		uint64_t mAcceptedSchedules = 0;
		uint64_t mCandidatesExamined = 0;
		bool mbPending = false;
		double mLastBaselineCost = 0;
		double mLastCandidateCost = 0;
		FArdaRHIStatus mLastStatus;
	};

	/** Retained completion receipt, valid after a frame slot is reused or the graph is edited. */
	struct FArdaDependencyFrameTicket
	{
		struct FState;

		explicit operator bool() const noexcept
		{
			return bool(mState);
		}

	private:
		eastl::shared_ptr<FState> mState;
		friend struct FArdaInductorRuntime;
	};
	struct FArdaInductorFrame;

	/** Frozen node data owned by the abstract graph. */
	struct FArdaDependencyNode
	{
		eastl::string mName;
		eastl::string mCanonicalKey;
		eastl::shared_ptr<const FArdaDependencyNodeDefinition> mDefinition;
		/** Frozen execution parameters, using mDefinition->mPreparedParameterType after optional preparation.
		 * Class-authored nodes retain a private wrapper here; callers must not cast it to the public input schema.
		 */
		eastl::shared_ptr<const void> mParameters;
		FArdaDependencyNodeDesc mDesc;
	};

	/** Distinguishes inferred resource dependencies from explicit ordering constraints. */
	struct FArdaDependencyEdge
	{
		bool mbResource = false;
	};

	using FArdaDependencyTopology = TArdaDirectedGraph<FArdaDependencyNode, FArdaDependencyEdge>;

	/** Persistent dependency graph, edited transactionally and executed repeatedly across frames.
	 * Graph values have one producer, so node attachment order never defines semantics.
	 * EndGraphEdit invokes ArdaInductor. A failed edit remains open for repair or cancellation.
	 */
	class FArdaDependencyGraph final
	{
	public:
		struct FImpl;
		explicit FArdaDependencyGraph(FArdaRHIDeviceRef Device = {});
		~FArdaDependencyGraph();
		FArdaDependencyGraph(const FArdaDependencyGraph&) = delete;
		FArdaDependencyGraph& operator=(const FArdaDependencyGraph&) = delete;
		FArdaRHIStatus BeginGraphEdit();
		FArdaRHIStatus EndGraphEdit();
		FArdaRHIStatus CancelGraphEdit();
		bool IsEditing() const noexcept;
		FArdaRHIStatus SetOptions(const FArdaInductorOptions& Options);
		TArdaRHIResult<FArdaDependencyResourceHandle> CreateResource(FArdaDependencyResourceDesc Desc);
		TArdaRHIResult<FArdaDependencyResourceHandle> CreateBuffer(eastl::string Name, FArdaRHIBufferDesc Desc);
		TArdaRHIResult<FArdaDependencyResourceHandle> CreateTexture(eastl::string Name, FArdaRHITextureDesc Desc);
		TArdaRHIResult<FArdaDependencyResourceHandle> ImportBuffer(eastl::string Name, FArdaRHIBufferRef Buffer);
		/** Retains texture storage. Used imports must have an initialized, uniform state and graphics
		 * ownership at compilation. Execution restores that captured state; callers preserve it between frames.
		 */
		TArdaRHIResult<FArdaDependencyResourceHandle> ImportTexture(eastl::string Name, FArdaRHITextureRef Texture);
		/** Retains native AS storage while graph nodes manage its builds, copies and reads. */
		TArdaRHIResult<FArdaDependencyResourceHandle> ImportAccelerationStructure(eastl::string Name,
		    FArdaRHIAccelStructRef AccelerationStructure);
		FArdaRHIStatus MarkOutput(FArdaDependencyResourceHandle Resource, bool Output = true);
		const FArdaDependencyResourceDesc* FindResource(FArdaDependencyResourceHandle Resource) const;
		FArdaGraphNodeHandle FindNode(const eastl::string& Name) const;
		const FArdaDependencyTopology& GetTopology() const;
		FArdaRHIStatus AddDependency(FArdaGraphNodeHandle Producer, FArdaGraphNodeHandle Consumer);
		FArdaRHIStatus RemoveDependency(FArdaGraphNodeHandle Producer, FArdaGraphNodeHandle Consumer);
		/** Removes resource-dependent descendants; manual ordering edges alone do not cascade deletion. */
		FArdaRHIStatus RemoveNode(FArdaGraphNodeHandle Node);

		/** Attach a class derived from TArdaDependencyNode or a domain specialization in ArdaDependencyNode.h.
		 * The base enforces the authoring hooks and owns registration, preparation caching and retention.
		 * Only new instances are prepared; an identical named instance is found before any setup runs.
		 * Preparation runs only inside an edit; failures propagate without compilation or GPU submission.
		 */
		template <class Node>
		TArdaRHIResult<FArdaGraphNodeHandle> AttachOrFind(eastl::string Name, typename Node::FParameters Parameters)
		{
			static_assert(std::is_base_of_v<FArdaDependencyNodeBase, Node>,
			    "Typed graph nodes must derive from TArdaDependencyNode or a domain specialization.");
			using Base = typename Node::FNodeBase;
			static_assert(std::is_same_v<typename Base::FNodeType, Node>, "The node base must name its derived class.");
			if (!IsEditing())
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Attach nodes inside a graph edit.")};
			}
			return Base::Attach(*this, eastl::move(Name), eastl::move(Parameters));
		}

		/** Attach a registered definition inside an edit (otherwise InvalidState). Name must be nonempty,
		 * Definition must exist, and P must match its registered parameter type (otherwise InvalidArgument).
		 * Reusing a name returns its existing handle only when definition identity and canonical key match;
		 * changing either requires removing that node first. Different names create distinct instances.
		 * Optional definition preparation runs after deduplication and before description, retaining its
		 * execution-schema result. Preparation errors leave the edit open without inserting a node.
		 * New instances retain const parameter snapshots and copies of described pipeline configurations;
		 * nested pointees are not deep-copied. Declared resources must be live versions owned by this graph.
		 * The call does not submit GPU work; EndGraphEdit resolves dependencies and compiles the schedule.
		 */
		template <class P>
		TArdaRHIResult<FArdaGraphNodeHandle> AttachOrFind(eastl::string Name,
		    const eastl::string& Definition,
		    P Parameters)
		{
			return AttachErased(eastl::move(Name),
			    Definition,
			    &ArdaDependencyParameterType<P>,
			    eastl::make_shared<const P>(eastl::move(Parameters)));
		}

		const FArdaInductorCompileResult& GetCompileResult() const;
		/** Snapshot of this graph's PSO cache; all counters are zero before its first pipeline compilation. */
		FArdaPipelineStateCacheStats GetPipelineCacheStats() const;
		/** CUDA capture/replay counters aggregated across the current compiled frame slots. */
		[[nodiscard]] FArdaCudaGraphStats GetCudaGraphStats() const;
		/** Submits a frame, waiting only when recycling an occupied frame slot. */
		TArdaRHIResult<FArdaDependencyFrameTicket> Submit(const FArdaGraphExecuteOptions& Options = {});
		FArdaGraphExecutionResult Wait(const FArdaDependencyFrameTicket& Ticket);
		TArdaRHIResult<bool> IsComplete(const FArdaDependencyFrameTicket& Ticket) const;
		/** Submit followed by Wait; use Submit for overlapping frames. */
		FArdaGraphExecutionResult Execute(const FArdaGraphExecuteOptions& Options = {});
		eastl::vector<FArdaInductorTimingSample> GetTimingProfile() const;
		/** Bounded raw history, ordered by frame sequence and node identity. Never waits for GPU work. */
		eastl::vector<FArdaInductorNodeTiming> GetTimingHistory() const;
		/** Polls available samples and advances automatic tuning without waiting for pending work. */
		void CollectTelemetry();
		FArdaInductorAdaptiveSchedulingStats GetAdaptiveSchedulingStats() const;
		void ClearTimingProfile();
		/** Requests a bounded background optimization using available samples. Poll stats/CollectTelemetry
		 * for adoption; the request does not wait for GPU completion or CPU search.
		 */
		FArdaRHIStatus OptimizeFromTimingProfile();
		FArdaRHIDeviceRef GetDevice() const;

	private:
		friend class FArdaInductor;
		TArdaRHIResult<FArdaGraphNodeHandle> AttachErased(eastl::string Name,
		    const eastl::string& Definition,
		    const void* Type,
		    eastl::shared_ptr<const void> Parameters);
		eastl::unique_ptr<FImpl> mImpl;
	};

	/** Compiler for semantic dependency resolution, CUDA grouping, queue selection and physical memory planning. */
	class FArdaInductor final
	{
	public:
		static FArdaRHIStatus Compile(FArdaDependencyGraph& Graph);
	};

	/** Typed callbacks resolve only their declared resources and automatically assigned pipeline slots. */
	class FArdaDependencyExecutionContext
	{
	public:
		IArdaRHICommandList& GetCommands() const;
		FArdaRHIDeviceRef GetDevice() const;
		FArdaRHIBufferRef GetBuffer(FArdaDependencyResourceHandle Resource) const;
		FArdaRHIBufferRef GetWorkspaceBuffer() const;
		uint32_t GetFrameIndex() const;
		uint64_t GetFrameSequence() const;
		/** Records an asynchronous readback. Execute waits after graph submission and
		 * publishes bytes only if every submission/completion succeeds. Failed frames
		 * clear registered destinations; unsubmitted command lists cancel their waits.
		 */
		FArdaRHIStatus ReadbackBuffer(FArdaDependencyResourceHandle Resource,
		    eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
		    uint64_t SourceOffset = 0,
		    uint64_t Size = ArdaRHIWholeBuffer) const;
		FArdaRHITextureRef GetTexture(FArdaDependencyResourceHandle Resource) const;
		/** Returns a declared acceleration structure access for the current node. */
		FArdaRHIAccelStructRef GetAccelerationStructure(FArdaDependencyResourceHandle Resource) const;
		const FArdaInductorResolvedPipeline* GetPipeline(const eastl::string& Slot = "default") const;
		FArdaRHIFramebufferRef GetFramebuffer() const;
		FArdaGraphNodeHandle GetNode() const;

	private:
		friend class FArdaInductor;
		friend class FArdaDependencyGraph;
		friend struct FArdaInductorRuntime;
		FArdaDependencyGraph::FImpl* mGraph = nullptr;
		FArdaInductorFrame* mFrame = nullptr;
		FArdaInductorPassContext* mPass = nullptr;
		FArdaGraphNodeHandle mNode;
	};
}
