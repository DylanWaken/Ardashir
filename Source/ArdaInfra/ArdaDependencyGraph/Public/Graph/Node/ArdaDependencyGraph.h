/** Persistent graph compilation, revision caching and execution. Author through FArdaGraphEdit. */
#pragma once

#include "Graph/Node/ArdaDependencyTopology.h"
#include "Graph/Node/ArdaGraphRevision.h"
#include "Graph/Resources/ArdaGraphExternal.h"
#include "Inductor/Config/ArdaInductorConfig.h"
#include "Inductor/Compiler/ArdaInductorCompileResult.h"
#include "Inductor/Runtime/ArdaDependencyFrame.h"
#include "Inductor/Runtime/ArdaDependencyGraphExecution.h"
#include "Inductor/Runtime/ArdaInductorTiming.h"
#include "RHI/Pipelines/ArdaPipelineStateCache.h"
#include <EASTL/type_traits.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/utility.h>
#include <filesystem>

namespace arda
{
	class FArdaDependencyNodeBase;

	/** Persistent dependency graph, edited transactionally and executed repeatedly across frames.
	 * Single-producer regions resolve independently of attachment order. Regions with multiple producers
	 * order reads and writes by original node attachment, preserving the preceding value until its readers finish.
	 * Disjoint regions remain independent. Public authoring uses resource operations and Node::Invoke.
	 * FArdaGraphEdit::Commit queues an ordered construction edit. Compile runs on the calling thread,
	 * selecting a cached revision or invoking ArdaInductor. Submit records and submits one frame on the
	 * calling thread and returns its GPU ticket. Execute also waits for frame completion.
	 * The next ordinary edit starts empty.
	 * Failed compilation retains queued edits for correction or DiscardPendingEdits.
	 * Separate threads may record and commit independent edit scopes concurrently. Explicit shared-resource
	 * revisions order those edits before hazard analysis; otherwise commit order defines attachment order.
	 * Each scope sees the construction committed before it opened. Graph inspection
	 * on an editing thread reads its private snapshot. A successful Compile locks authoring until a
	 * Submit/Execute attempt; later reference-returning inspection remains valid after that attempt returns.
	 */
	class FArdaDependencyGraph final
	{
	public:
		struct FArdaImpl;
		explicit FArdaDependencyGraph(FArdaRHIDeviceRef Device = {});
		~FArdaDependencyGraph();
		FArdaDependencyGraph(const FArdaDependencyGraph&) = delete;
		FArdaDependencyGraph& operator=(const FArdaDependencyGraph&) = delete;

		/** Declare a contextual value slot. TypeName is a stable application-owned schema name. */
		template <class T>
		FArdaRHIStatus DeclareExternalValue(eastl::string Name, eastl::string TypeName)
		{
			return ApplyExternalValueBinding(
			    {eastl::move(Name), eastl::move(TypeName), &ArdaGraphExternalType<T>, nullptr, {}});
		}

		/** Bind a named caller-owned value. Supply TypeName when first declaring the slot.
		 * Raw pointers remain borrowed; shared and native references are retained by the execution plan.
		 */
		template <class T>
		FArdaRHIStatus BindExternalValue(eastl::string Name, T Value, eastl::string TypeName = {})
		{
			const auto Identity = TArdaGraphExternalReference<T>::Identity(Value);
			if constexpr (TArdaGraphExternalReference<T>::mReference)
			{
				if (!Identity)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "An external reference binding cannot be null; declare a placeholder instead.");
				}
			}
			return ApplyExternalValueBinding({eastl::move(Name),
			    eastl::move(TypeName),
			    &ArdaGraphExternalType<T>,
			    Identity,
			    eastl::make_shared<const T>(eastl::move(Value))});
		}

		/** Enumerate host, shader, callback and other context-owned value slots. */
		eastl::vector<FArdaGraphExternalValueSlot> GetExternalValueSlots(bool bOnlyUnbound = false) const;
		/** Return a structurally preprocessed graph without compiling or requiring native external bindings. */
		[[nodiscard]] TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> Preprocess() const;
		/** Whether structural preprocessing has already been applied to this graph. */
		[[nodiscard]] bool IsPreprocessed() const;
		/** Inspect stable external slot names, kinds and expected descriptors; optionally return only unbound slots. */
		[[nodiscard]] eastl::vector<FArdaDependencyResourceDesc> GetExternalResourceSlots(
		    bool bOnlyUnbound = false) const;
		/** Bind compatible buffer storage to a declared external slot, invalidating prior executable revisions. */
		[[nodiscard]] FArdaRHIStatus BindExternalResource(const eastl::string& Name, FArdaRHIBufferRef Buffer);
		/** Bind compatible texture storage to a declared external slot, invalidating prior executable revisions. */
		[[nodiscard]] FArdaRHIStatus BindExternalResource(const eastl::string& Name, FArdaRHITextureRef Texture);
		/** Bind compatible acceleration-structure storage to a declared external slot and invalidate executables. */
		[[nodiscard]] FArdaRHIStatus BindExternalResource(const eastl::string& Name, FArdaRHIAccelStructRef Structure);
		/** Encode the closed construction's named nodes, public parameters, resources and connections.
		 * External bindings become named placeholders. Execution objects are excluded; unsupported public fields fail.
		 */
		[[nodiscard]] TArdaRHIResult<eastl::string> Serialize() const;
		/** Restore a complete archive into a new graph with a queued construction; call Compile before execution.
		 * Class-authored node types must be registered; portable definitions and initial buffer bytes are embedded.
		 * Incompatible versions fail, and failure publishes no partial graph or portable definition library.
		 */
		[[nodiscard]] static TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> Deserialize(
		    const eastl::string& Data,
		    FArdaRHIDeviceRef Device = {});
		/** Serialize the closed construction and atomically replace Path through backend FileOperations. */
		[[nodiscard]] FArdaRHIStatus Save(const std::filesystem::path& Path) const;
		/** Read an archive through backend FileOperations and restore a new graph with queued construction. */
		[[nodiscard]] static TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> Load(
		    const std::filesystem::path& Path,
		    FArdaRHIDeviceRef Device = {});
		/** Compile on the calling thread and return final diagnostics.
		 * Apply the ordered queue and stage a persistent executable revision. Matching constructions
		 * reuse cached topology, compilation and runtime. Success clears the queue and locks edits until
		 * Execute/Submit is attempted. Failure preserves the queue for repair or DiscardPendingEdits.
		 * With no queued construction, re-stage the current persistent graph. Force bypasses cache lookup.
		 * Switching revisions retires the outgoing runtime's submissions; reusing that same runtime preserves
		 * frame-slot overlap. Requires closed edits and no outstanding staged execution lock.
		 * Compiler rules and callbacks run on this thread. Reentrant graph mutation is rejected.
		 */
		[[nodiscard]] FArdaRHIStatus Compile(bool bForceRecompile = false);

		/** Alias for Compile retained for synchronous callers. */
		[[nodiscard]] FArdaRHIStatus CompileAndWait(bool bForceRecompile = false)
		{
			return Compile(bForceRecompile);
		}
		/** Discard all queued construction transactions and restore the last persistent revision.
		 * Requires no open edit and no staged execution lock.
		 */
		FArdaRHIStatus DiscardPendingEdits();
		/** Remove reusable cache entries. The staged executable remains valid; the next Compile rebuilds.
		 * Invalid during an edit, compilation or execution callback.
		 */
		FArdaRHIStatus InvalidatePersistentCache();
		/** Set the maximum retained revisions (default four, minimum one); evicted GPU work is retired. */
		FArdaRHIStatus SetRevisionCacheCapacity(uint32_t Capacity);
		/** Inspect queue state, persistent identity and graph-wide cache/compilation counters. */
		FArdaGraphRevisionStats GetRevisionStats() const;
		bool IsEditing() const noexcept;
		/** Read current construction/revision options. Do not retain the reference across revision changes. */
		const FArdaInductorOptions& GetOptions() const;
		const FArdaDependencyResourceDesc* FindResource(FArdaDependencyResourceHandle Resource) const;
		/** Resolve a declared resource name through preprocessing aliases, or return an empty handle. */
		FArdaDependencyResourceHandle FindResource(const eastl::string& Name) const;
		FArdaGraphNodeHandle FindNode(const eastl::string& Name) const;
		/** Returns a node-declared output, or an invalid handle for an unknown node/slot.
		 * Resolve newly attached outputs before Compile, which may select another cached topology's node handles.
		 */
		FArdaDependencyResourceHandle FindOutput(FArdaGraphNodeHandle Node, const eastl::string& Name) const;
		const FArdaDependencyTopology& GetTopology() const;
		/** Result-owned description for a current node, including this thread's pending edit. */
		const FArdaDependencyNodeDesc& GetNodeDescription(FArdaGraphNodeHandle Node) const;
		const FArdaInductorCompileResult& GetCompileResult() const;
		/** Snapshot of this graph's PSO cache; all counters are zero before its first pipeline compilation. */
		FArdaPipelineStateCacheStats GetPipelineCacheStats() const;
		/** CUDA capture/replay counters aggregated across the current compiled frame slots. */
		[[nodiscard]] FArdaCudaGraphStats GetCudaGraphStats() const;
		/** Record and submit one frame on the calling thread, returning its GPU frame ticket.
		 * Materialize the pending plan on first submission, then submit a frame. Preparation failure
		 * returns a diagnostic without publishing a frame and retains the plan for retry.
		 * Later submissions reuse the runtime, waiting only when recycling an occupied frame slot.
		 * Before hardware work begins, Compile must have succeeded with no pending construction.
		 * Attempting submission unlocks editing, including on submission failure; another submission
		 * without new edits retries the selected revision. The ticket may still have GPU work in flight;
		 * poll with IsComplete(ticket). Wait(ticket) retires the frame and publishes readbacks.
		 */
		[[nodiscard]] TArdaRHIResult<FArdaDependencyFrameTicket> Submit(const FArdaGraphExecuteOptions& Options = {});
		/** Alias for Submit retained for synchronous callers. */
		TArdaRHIResult<FArdaDependencyFrameTicket> SubmitAndWait(const FArdaGraphExecuteOptions& Options = {});
		FArdaGraphExecutionResult Wait(const FArdaDependencyFrameTicket& Ticket);
		TArdaRHIResult<bool> IsComplete(const FArdaDependencyFrameTicket& Ticket) const;
		/** Submit one frame and join GPU completion on the calling thread, returning the frame result.
		 * Submission unlocks editing first, so another thread may author the next construction while
		 * this call waits for completion.
		 */
		[[nodiscard]] FArdaGraphExecutionResult Execute(const FArdaGraphExecuteOptions& Options = {});
		/** Alias for Execute retained for synchronous callers. */
		FArdaGraphExecutionResult ExecuteAndWait(const FArdaGraphExecuteOptions& Options = {});
		eastl::vector<FArdaInductorTimingSample> GetTimingProfile() const;
		/** Bounded raw history, ordered by frame sequence and node identity. Never waits for GPU work. */
		eastl::vector<FArdaInductorNodeTiming> GetTimingHistory() const;
		/** Polls available samples and advances automatic tuning without waiting for pending work. */
		void CollectTelemetry();
		FArdaInductorAdaptiveSchedulingStats GetAdaptiveSchedulingStats() const;
		void ClearTimingProfile();
		/** Prepares a bounded schedule candidate on the calling thread. CollectTelemetry polls for safe adoption. */
		FArdaRHIStatus OptimizeFromTimingProfile();
		FArdaRHIDeviceRef GetDevice() const;

	private:
		FArdaRHIStatus ApplyExternalValueBinding(FArdaGraphExternalValueBinding Binding);
		/** Open the next transaction. A new post-execution construction starts empty by default.
		 * Additional edits before Compile extend the queued construction. Compiled graphs remain locked
		 * until Execute/Submit is attempted; authoring never mutates a persistent revision.
		 */
		FArdaRHIStatus BeginGraphEdit(FArdaGraphEdit& Edit);
		/** Close the open transaction and append its semantic hash and declarations to the ordered edit queue.
		 * Does not compile or submit GPU work. Additional edits extend this construction until Compile succeeds.
		 * Execute is unavailable while editing or while queued changes await compilation.
		 * Requires an open private scope and an unlocked queue. Publication is serialized internally.
		 */
		FArdaRHIStatus EndGraphEdit(FArdaGraphEdit& Edit);
		/** Discard only this private transaction and immediately release its retained declarations and state.
		 * Other scopes and queued edits remain intact. Canceling the last uncommitted construction restores the persistent revision.
		 * Does not allocate or rebuild native resources; use DiscardPendingEdits to discard closed edits.
		 * Requires an open edit; returns InvalidState during compilation or without an edit.
		 */
		FArdaRHIStatus CancelGraphEdit(FArdaGraphEdit& Edit);
		FArdaRHIStatus SetOptions(const FArdaInductorOptions& Options);
		TArdaRHIResult<FArdaDependencyResourceHandle> CreateResource(FArdaDependencyResourceDesc Desc);
		TArdaRHIResult<FArdaDependencyResourceHandle> CreateBuffer(eastl::string Name, FArdaRHIBufferDesc Desc);
		TArdaRHIResult<FArdaDependencyResourceHandle> CreateTexture(eastl::string Name, FArdaRHITextureDesc Desc);
		TArdaRHIResult<FArdaDependencyResourceHandle> ImportBuffer(eastl::string Name, FArdaRHIBufferRef Buffer);
		/** Retains texture storage. Used imports must have an initialized, uniform state and graphics
		 * ownership at first submission. Execution restores that captured state; callers preserve it between frames.
		 */
		TArdaRHIResult<FArdaDependencyResourceHandle> ImportTexture(eastl::string Name, FArdaRHITextureRef Texture);
		/** Retains native AS storage while graph nodes manage its builds, copies and reads. */
		TArdaRHIResult<FArdaDependencyResourceHandle> ImportAccelerationStructure(eastl::string Name,
		    FArdaRHIAccelStructRef AccelerationStructure);
		FArdaRHIStatus MarkOutput(FArdaDependencyResourceHandle Resource, bool Output = true);
		FArdaRHIStatus AddDependency(FArdaGraphNodeHandle Producer, FArdaGraphNodeHandle Consumer);

		/** Attach a class derived from TArdaNodeDefinition in ArdaDependencyNode.h.
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
			static_assert(eastl::is_base_of_v<FArdaDependencyNodeBase, Node>,
			    "Typed graph nodes must derive from TArdaNodeDefinition.");
			using FArdaBase = typename Node::FArdaNodeBase;
			static_assert(eastl::is_same_v<typename FArdaBase::FArdaNodeType, Node>,
			    "The node base must name its derived class.");
			if (!IsEditing())
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Attach nodes inside a graph edit.")};
			}
			return FArdaBase::Attach(*this, eastl::move(Name), eastl::move(Parameters));
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
		 * The call does not submit GPU work. EndGraphEdit queues declarations; Compile resolves dependencies
		 * or selects an identical persistent revision. Fresh frame constructions need no removal of old nodes.
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

		const FArdaImpl& GetReadImpl() const;
		FArdaRHIStatus RevalidateCompilationParameters();
		FArdaRHIStatus MergeGraphEdit(FArdaGraphEdit& Edit, FArdaDependencyGraph& Destination);
		void RefreshConstructionKeys(FArdaImpl& Graph);
		struct FArdaRevisionState;
		friend class FArdaGraphEdit;
		friend class FArdaGraphAccess;
		friend class FArdaGraphSerialization;
		friend class FArdaPreprocessor;
		friend class FArdaInductorGraph;
		friend class FArdaGraphExecutor;
		/** Publish one validated binding while retiring incompatible runtime/cache state atomically. */
		FArdaRHIStatus ApplyExternalResourceBinding(FArdaDependencyResourceDesc Resource);
		friend class FArdaSubgraphPreprocessor;
		template <class, class, EArdaDependencyNodeKind>
		friend class node_detail::TArdaExecutionNode;
		friend class FArdaInductor;
		friend class FArdaGraphRule;
		friend class FArdaDependencyResourceContext;
		TArdaRHIResult<FArdaGraphNodeHandle> AttachErased(eastl::string Name,
		    const eastl::string& Definition,
		    const void* Type,
		    eastl::shared_ptr<const void> Parameters,
		    eastl::shared_ptr<const FArdaDependencyNodeExecutable> RetainedDefinition = {});
		eastl::shared_ptr<FArdaRevisionState> mRevisions;
		eastl::shared_ptr<FArdaImpl>& mImpl;
	};
}
