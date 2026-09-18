#pragma once

#include "ArdaGraph.h"
#include "ArdaDependencyGraphExecution.h"
#include "ArdaDependencyRequirements.h"
#include "RHI/Scheduling/ArdaCudaSequence.h"
#include "ArdaInductorPipeline.h"
#include "RHI/Shaders/ArdaShaderParameters.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <mutex>
#include <type_traits>
#include <utility>

namespace arda
{
	/** A graph-owned logical resource. Repeated writes to a region follow node attachment order. */
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
	/** ReadWrite consumes the preceding value and writes in place; initial contents require external storage. */
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

	/** Logical argument for a named shader-struct leaf. Register information comes from its metadata. */
	struct FArdaDependencyShaderResource
	{
		eastl::string mMember;
		FArdaDependencyResourceHandle mResource;
		uint32_t mArrayElement = 0;
		/** UAV access; SRV and constant-buffer access is always Read. */
		EArdaDependencyAccess mAccess = EArdaDependencyAccess::Write;
		FArdaRHIViewDesc mView;
		/** Samplers have no graph allocation or data dependency. */
		FArdaRHISamplerRef mSampler;
	};

	/** Writes a populated table entry's index into a uint32_t field of frozen shader parameter bytes. */
	struct FArdaDependencyDescriptorIndex
	{
		eastl::string mTable;
		uint32_t mSlot = 0;
		uint32_t mArrayElement = 0;
		EArdaRHIBindingType mType = EArdaRHIBindingType::StructuredBufferSRV;
		uint32_t mByteOffset = 0;
		/** Native heap address, supported only for direct-heap tables with one descriptor bank. */
		bool mbAbsoluteHeapIndex = false;
	};

	/** Bytes copied from node parameters at attachment; named push-constant leaf in the shader schema. */
	struct FArdaDependencyShaderValue
	{
		eastl::string mMember;
		eastl::vector<uint8_t> mBytes;
		eastl::vector<FArdaDependencyDescriptorIndex> mDescriptorIndices;
	};

	/** Frozen shader schema and logical arguments for executor-owned binding preparation. */
	struct FArdaDependencyShaderBindings
	{
		const FArdaShaderParameterMetadata* mMetadata = nullptr;
		eastl::string mPipelineSlot = "default";
		eastl::vector<FArdaDependencyShaderResource> mResources;
		eastl::vector<FArdaDependencyShaderValue> mValues;
	};

	/** A populated descriptor and its complete possible data access. Indices do not establish hazards. */
	struct FArdaDependencyBindlessEntry
	{
		uint32_t mSlot = 0;
		uint32_t mArrayElement = 0;
		EArdaRHIBindingType mType = EArdaRHIBindingType::StructuredBufferSRV;
		FArdaDependencyResourceHandle mResource;
		FArdaRHIViewDesc mView;
		EArdaDependencyAccess mAccess = EArdaDependencyAccess::Read;
		FArdaRHISamplerRef mSampler;
	};

	/** Immutable node-local table declaration. The graph creates its layout and per-frame descriptor tables.
     * List every entry the shader might access, including GPU-selected indices. Different nodes may declare
     * different entries of the same layout. Capacity is bounded; holes must never be indexed by the shader.
     */
	struct FArdaDependencyBindlessTable
	{
		eastl::string mName;
		eastl::string mPipelineSlot = "default";
		FArdaRHIBindlessLayoutDesc mLayout;
		/** Zero selects one past the greatest supplied index. Must fit mLayout.mMaxCapacity. */
		uint32_t mCapacity = 0;
		eastl::vector<FArdaDependencyBindlessEntry> mEntries;
	};

	/** Declarative ray record; local shader resources are resolved against the export's local layout. */
	struct FArdaDependencyShaderTableRecord
	{
		EArdaRHIShaderTableRecordType mType = EArdaRHIShaderTableRecordType::RayGeneration;
		/** Dense zero-based index within mType, not the native table's flat record index. */
		uint32_t mRecordIndex = 0;
		eastl::string mExportName;
		FArdaDependencyShaderBindings mLocalBindings;
		eastl::vector<uint8_t> mLocalArguments;
		uint32_t mUserData = 0;
		FArdaDependencyResourceHandle mGeometry;
		uint32_t mGeometrySegment = 0;
	};

	/** Optional explicit record order. Without a declaration, a ray slot receives its unique raygen,
     * then every miss/hit-group/callable in the resolved pipeline's deterministic export order.
     */
	struct FArdaDependencyShaderTable
	{
		eastl::string mPipelineSlot = "default";
		eastl::vector<FArdaDependencyShaderTableRecord> mRecords;
	};

	/** Compile-time declaration returned by a registered node's parameter visitor. */
	struct FArdaDependencyNodeDesc
	{
		eastl::vector<FArdaDependencyAccess> mAccesses;
		/** Created after physical allocation and pipeline resolution, separately per frame slot. */
		eastl::vector<FArdaDependencyShaderBindings> mShaderBindings;
		eastl::vector<FArdaDependencyBindlessTable> mBindlessTables;
		eastl::vector<FArdaDependencyShaderTable> mShaderTables;
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
		/** Graph-populated admission facts from GetRequirements plus recognized descriptor features.
		 * Authors declare explicit requirements through the node/definition hook, not this output field.
		 */
		FArdaDependencyNodeRequirements mRequirements;

		/** Declare shader arguments. Attachment derives hazards from metadata; compilation creates
		 * retained binding sets. Metadata has static lifetime. Declare non-shader accesses separately.
		 */
		template <class ShaderParameters>
		void BindShader(eastl::vector<FArdaDependencyShaderResource> Resources,
		    eastl::string Slot = "default",
		    eastl::vector<FArdaDependencyShaderValue> Values = {})
		{
			mShaderBindings.push_back({&ShaderParameters::GetStaticMetadata(),
			    eastl::move(Slot),
			    eastl::move(Resources),
			    eastl::move(Values)});
		}
	};

	class FArdaInductorPassContext;
	class FArdaDependencyExecutionContext;
	class FArdaDependencyNodeBase;
	class FArdaDependencyGraph;

	/** One named output exposed by a node's resource declaration. */
	struct FArdaDependencyNodeOutput
	{
		eastl::string mName;
		FArdaDependencyResourceHandle mResource;
	};

	/** Attachment-time resource declarations. Empty outputs become graph-owned transients;
	 * supplied outputs are validated against the required description. No GPU allocation occurs here.
	 * Outputs are transactional and can be retrieved using Graph.FindOutput(Node, Name).
	 */
	class FArdaDependencyResourceContext
	{
	public:
		FArdaRHIStatus Buffer(FArdaDependencyResourceHandle& Output, eastl::string Name, FArdaRHIBufferDesc Desc);
		FArdaRHIStatus Texture(FArdaDependencyResourceHandle& Output, eastl::string Name, FArdaRHITextureDesc Desc);
		const FArdaDependencyResourceDesc* Find(FArdaDependencyResourceHandle Input) const;

	private:
		friend class FArdaDependencyGraph;
		FArdaDependencyResourceContext(FArdaDependencyGraph& Graph,
		    eastl::string Name,
		    eastl::vector<FArdaDependencyNodeOutput> Existing);
		FArdaRHIStatus Declare(FArdaDependencyResourceHandle& Output,
		    eastl::string Name,
		    FArdaDependencyResourceDesc Desc);
		FArdaDependencyGraph& mGraph;
		eastl::string mName;
		eastl::vector<FArdaDependencyNodeOutput> mExisting, mOutputs;
	};

	/** Framework-owned erased implementation generated only from the common class node contract.
	 * Registry lookup exposes a const view for graph/compiler inspection; this is not an authoring API.
	 */
	class FArdaDependencyNodeExecutable final
	{
	private:
		template <class Derived, class Parameters, EArdaDependencyNodeKind Kind>
		friend class TArdaDependencyNode;
		friend class FArdaNodeRegistry;

		FArdaDependencyNodeExecutable() = default;
		FArdaDependencyNodeExecutable(FArdaDependencyNodeExecutable&&) = default;
		FArdaDependencyNodeExecutable(const FArdaDependencyNodeExecutable&) = delete;
		FArdaDependencyNodeExecutable& operator=(const FArdaDependencyNodeExecutable&) = delete;

	public:
		~FArdaDependencyNodeExecutable() = default;
		eastl::string mName;
		uint32_t mVersion = 1;
		EArdaDependencyNodeKind mKind = EArdaDependencyNodeKind::Compute;
		/** Public attachment schema used for input type validation and canonical identity, before preparation.
		 * Typed registration assigns ArdaDependencyParameterType<Parameters>; execution can use a different schema.
		 */
		const void* mParameterType = nullptr;
		/** Private bound-parameter schema generated by the class base for description and execution. */
		const void* mPreparedParameterType = nullptr;
		/** Read-only preflight on public parameters, before output declarations or device preparation.
		 * Empty means no explicit requirements. Called on matching reattachment as well.
		 */
		eastl::function<FArdaDependencyNodeRequirements(const void*)> mGetRequirements;
		/** Resolves node-owned outputs before canonical identity and preparation. */
		eastl::function<TArdaRHIResult<eastl::shared_ptr<const void>>(FArdaDependencyResourceContext&,
		    eastl::shared_ptr<const void>)>
		    mResolveResources;
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

	template <class T>
	inline const uint8_t ArdaDependencyParameterType = 0;

	/** Process-wide registry. Lookup returns retained immutable definitions; registration is synchronized. */
	class FArdaNodeRegistry final
	{
	public:
		static FArdaNodeRegistry& Get();
		/** Removes library lookup; existing graph instances retain their immutable definition. */
		FArdaRHIStatus Unregister(const eastl::string& Name);
		eastl::shared_ptr<const FArdaDependencyNodeExecutable> Find(const eastl::string& Name) const;
		eastl::vector<eastl::string> GetNames() const;

	private:
		template <class Derived, class Parameters, EArdaDependencyNodeKind Kind>
		friend class TArdaDependencyNode;

		FArdaRHIStatus RegisterExecutable(FArdaDependencyNodeExecutable Executable);
		mutable std::mutex mMutex;
		eastl::unordered_map<eastl::string, eastl::shared_ptr<const FArdaDependencyNodeExecutable>> mDefinitions;
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
		struct FArdaState;

		explicit operator bool() const noexcept
		{
			return bool(mState);
		}

	private:
		eastl::shared_ptr<FArdaState> mState;
		friend struct FArdaInductorRuntime;
	};
	struct FArdaInductorFrame;

	/** Frozen node data owned by the abstract graph. */
	struct FArdaDependencyNode
	{
		eastl::string mName;
		eastl::string mCanonicalKey;
		eastl::shared_ptr<const FArdaDependencyNodeExecutable> mDefinition;
		/** Frozen execution parameters, using mDefinition->mPreparedParameterType after optional preparation.
		 * Class-authored nodes retain a private wrapper here; callers must not cast it to the public input schema.
		 */
		eastl::shared_ptr<const void> mParameters;
		FArdaDependencyNodeDesc mDesc;
		eastl::vector<FArdaDependencyNodeOutput> mOutputs;
		/** Original successful attachment order, preserved by deduplication and edit snapshots. */
		uint64_t mAttachmentOrder = 0;
	};

	/** Value dependencies drive removal; overwrite hazards constrain execution without consuming a value. */
	struct FArdaDependencyEdge
	{
		bool mbResource = false;
		bool mbHazard = false;
	};

	using FArdaDependencyTopology = TArdaDirectedGraph<FArdaDependencyNode, FArdaDependencyEdge>;

	/** Persistent dependency graph, edited transactionally and executed repeatedly across frames.
	 * Single-producer regions resolve independently of attachment order. Regions with multiple producers
	 * order reads and writes by original node attachment, preserving the preceding value until its readers finish.
	 * Disjoint regions remain independent. Reattaching an existing node does not change its order.
	 * EndGraphEdit invokes ArdaInductor. A failed edit remains open for repair or cancellation.
	 */
	class FArdaDependencyGraph final
	{
	public:
		struct FArdaImpl;
		explicit FArdaDependencyGraph(FArdaRHIDeviceRef Device = {});
		~FArdaDependencyGraph();
		FArdaDependencyGraph(const FArdaDependencyGraph&) = delete;
		FArdaDependencyGraph& operator=(const FArdaDependencyGraph&) = delete;
		FArdaRHIStatus BeginGraphEdit();
		/** Compile the open edit, preparing allocations, pipelines and per-frame shader bindings.
		 * On success publishes the new executable graph and closes the edit.
		 * On failure returns the diagnostic status and leaves the edit open for repair or CancelGraphEdit.
		 * Execute is unavailable while editing. Call only after BeginGraphEdit, with external synchronization.
		 */
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
		/** Returns a node-declared output, or an invalid handle for an unknown node/slot. */
		FArdaDependencyResourceHandle FindOutput(FArdaGraphNodeHandle Node, const eastl::string& Name) const;
		const FArdaDependencyTopology& GetTopology() const;
		FArdaRHIStatus AddDependency(FArdaGraphNodeHandle Producer, FArdaGraphNodeHandle Consumer);
		FArdaRHIStatus RemoveDependency(FArdaGraphNodeHandle Producer, FArdaGraphNodeHandle Consumer);
		/** Removes this node and recursively removes readers consuming its produced values.
		 * Overwrite hazards and manual ordering edges alone do not cascade deletion.
		 */
		FArdaRHIStatus RemoveNode(FArdaGraphNodeHandle Node);

		/** Attach a class derived from TArdaDependencyNode or a domain specialization in ArdaDependencyNode.h.
		 * The base enforces the authoring hooks and owns registration, preparation caching and retention.
		 * Only new instances are prepared; an identical named instance is found before any setup runs.
		 * Finding an existing instance preserves its original attachment order.
		 * Explicit GetRequirements is checked before output declaration or preparation, on every call.
		 * Recognized descriptor requirements are checked after Describe, before graph binding layouts.
		 * Unsupported reports the node name and missing capabilities; no node is inserted and provisional
		 * outputs are rolled back. Empty explicit requirements allow device-less semantic analysis;
		 * inferred requirements are retained but not enforced without a device.
		 * Preparation runs only inside an edit; failures propagate without compilation or GPU submission.
		 * Each node type declares its own FArdaParameters struct. Pass a prefilled value or an inline aggregate;
		 * every new named node retains its own immutable snapshot, including across different graphs.
		 */
		template <class Node>
		TArdaRHIResult<FArdaGraphNodeHandle> AttachOrFind(eastl::string Name, typename Node::FArdaParameters Parameters)
		{
			static_assert(std::is_base_of_v<FArdaDependencyNodeBase, Node>,
			    "Typed graph nodes must derive from TArdaDependencyNode or a domain specialization.");
			using FArdaBase = typename Node::FArdaNodeBase;
			static_assert(std::is_same_v<typename FArdaBase::FArdaNodeType, Node>,
			    "The node base must name its derived class.");
			if (!IsEditing())
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Attach nodes inside a graph edit.")};
			}
			return FArdaBase::Attach(*this, eastl::move(Name), eastl::move(Parameters));
		}

		/** Fill a fresh, value-initialized Node::FArdaParameters during attachment, then freeze its snapshot.
		 * Initialize receives FArdaParameters& synchronously and returns void or FArdaRHIStatus. It runs once
		 * per call inside an edit, including reattachment, before registration, requirements or preparation.
		 * A failed status aborts attachment. Do not retain the reference or mutate the graph in Initialize.
		 */
		template <class Node,
		    class Initializer,
		    std::enable_if_t<std::is_invocable_v<Initializer, typename Node::FArdaParameters&>, int> = 0>
		TArdaRHIResult<FArdaGraphNodeHandle> AttachOrFind(eastl::string Name, Initializer&& Initialize)
		{
			static_assert(std::is_base_of_v<FArdaDependencyNodeBase, Node>,
			    "Typed graph nodes must derive from TArdaDependencyNode or a domain specialization.");
			using FArdaBase = typename Node::FArdaNodeBase;
			static_assert(std::is_same_v<typename FArdaBase::FArdaNodeType, Node>,
			    "The node base must name its derived class.");
			return FArdaBase::Attach(*this, eastl::move(Name), std::forward<Initializer>(Initialize));
		}

		/** Attach a registered definition inside an edit (otherwise InvalidState). Name must be nonempty,
		 * Definition must exist, and P must match its registered parameter type (otherwise InvalidArgument).
		 * Reusing a name returns its existing handle only when definition identity and canonical key match;
		 * changing either requires removing that node first. Different names create distinct instances.
		 * Finding an existing instance preserves its original attachment order.
		 * The definition's mGetRequirements receives public parameters on every call, before resolving outputs
		 * or preparation. Unsatisfied requirements return Unsupported with the node name and missing abilities.
		 * A new description is checked for recognized feature requirements before graph binding preparation.
		 * The common class base prepares a new instance after deduplication and before description, retaining its
		 * private bound parameters. Preparation errors leave the edit open without inserting a node.
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
		friend class FArdaDependencyResourceContext;
		TArdaRHIResult<FArdaGraphNodeHandle> AttachErased(eastl::string Name,
		    const eastl::string& Definition,
		    const void* Type,
		    eastl::shared_ptr<const void> Parameters);
		eastl::unique_ptr<FArdaImpl> mImpl;
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
		/** Records a graphics-queue texture readback with completion-safe publication.
		 * Resource must declare the selected subresource in CopySource state. Supports typed,
		 * single-sample, uncompressed color regions. Output contains tightly packed rows in increasing
		 * Y order, followed by increasing Z slices; native row padding is removed after completion.
		 * The retained destination follows ReadbackBuffer's successful-frame and failure semantics.
		 * Declare mTransientWorkspaceBytes at least GetArdaRHITextureBufferFootprint(...).mValue.mByteSize.
		 * The graph budgets and allocates this workspace per frame; its UAV state is restored after recording.
		 */
		FArdaRHIStatus ReadbackTexture(FArdaDependencyResourceHandle Resource,
		    eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
		    const FArdaRHITextureSlice& Slice = {}) const;
		FArdaRHITextureRef GetTexture(FArdaDependencyResourceHandle Resource) const;
		/** Returns a declared acceleration structure access for the current node. */
		FArdaRHIAccelStructRef GetAccelerationStructure(FArdaDependencyResourceHandle Resource) const;
		const FArdaInductorResolvedPipeline* GetPipeline(const eastl::string& Slot = "default") const;
		/** Retained sets prepared for this node/frame slot; lookup performs no binding creation. */
		const eastl::vector<FArdaRHIBindingSetRef>& GetBindings(const eastl::string& Slot = "default") const;
		/** Bind the compiled compute pipeline and its automatically prepared shader arguments. */
		/** Prepared ray table and descriptor tables remain stable throughout a compiled frame slot. */
		const FArdaRHIShaderTableRef& GetShaderTable(const eastl::string& Slot = "default") const;
		const FArdaRHIDescriptorTableRef& GetDescriptorTable(const eastl::string& Name) const;
		FArdaRHIStatus SetRayTracingState(const eastl::string& Slot = "default") const;
		FArdaRHIStatus DispatchWorkGraph(const void* Records,
		    uint32_t RecordCount,
		    uint32_t RecordStride,
		    const eastl::string& Slot = "default") const;
		/** Apply the frozen push-constant bytes declared by BindShader; state helpers call this automatically. */
		FArdaRHIStatus ApplyShaderParameters(const eastl::string& Slot = "default") const;
		FArdaRHIStatus SetComputeState(const eastl::string& Slot = "default") const;
		/** Complete draw state with the compiled pipeline, framebuffer and shader bindings. */
		FArdaRHIStatus SetGraphicsState(FArdaRHIGraphicsState State, const eastl::string& Slot = "default") const;
		/** Complete mesh state with the compiled pipeline, framebuffer and shader bindings. */
		FArdaRHIStatus SetMeshletState(FArdaRHIMeshletState State, const eastl::string& Slot = "default") const;
		FArdaRHIFramebufferRef GetFramebuffer() const;
		FArdaGraphNodeHandle GetNode() const;

	private:
		friend class FArdaInductor;
		friend class FArdaDependencyGraph;
		friend struct FArdaInductorRuntime;
		FArdaDependencyGraph::FArdaImpl* mGraph = nullptr;
		FArdaInductorFrame* mFrame = nullptr;
		FArdaInductorPassContext* mPass = nullptr;
		FArdaGraphNodeHandle mNode;
	};
}
