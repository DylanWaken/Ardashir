/** Persistent graph compilation, revision caching and execution. Author through FArdaGraphEdit. */
#pragma once

#include "Graph/Node/ArdaDependencyTopology.h"
#include "Graph/Node/ArdaGraphRevision.h"
#include "Inductor/Config/ArdaInductorConfig.h"
#include "Inductor/Compiler/ArdaInductorCompileResult.h"
#include "Inductor/Runtime/ArdaDependencyFrame.h"
#include "Inductor/Runtime/ArdaDependencyGraphExecution.h"
#include "Inductor/Runtime/ArdaInductorTiming.h"
#include "RHI/Pipelines/ArdaPipelineStateCache.h"
#include <EASTL/type_traits.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/utility.h>

namespace arda
{
	class FArdaDependencyNodeBase;

	/** Persistent dependency graph, edited transactionally and executed repeatedly across frames.
	 * Single-producer regions resolve independently of attachment order. Regions with multiple producers
	 * order reads and writes by original node attachment, preserving the preceding value until its readers finish.
	 * Disjoint regions remain independent. Public authoring uses resource operations and Node::Invoke.
	 * FArdaGraphEdit::Commit queues an ordered construction edit. Compile selects a cached persistent revision or
	 * invokes ArdaInductor, then locks edits until execution is attempted. The next ordinary edit starts empty.
	 * Failed compilation retains queued edits for correction or DiscardPendingEdits. Externally synchronize access.
	 */
	class FArdaDependencyGraph final
	{
	public:
		struct FArdaImpl;
		explicit FArdaDependencyGraph(FArdaRHIDeviceRef Device = {});
		~FArdaDependencyGraph();
		FArdaDependencyGraph(const FArdaDependencyGraph&) = delete;
		FArdaDependencyGraph& operator=(const FArdaDependencyGraph&) = delete;
		/** Apply the ordered queue and stage a persistent executable revision. Matching constructions
		 * reuse cached topology, compilation and runtime. Success clears the queue and locks edits until
		 * Execute/Submit is attempted. Failure preserves the queue for repair or DiscardPendingEdits.
		 * With no queued construction, re-stage the current persistent graph. Force bypasses cache lookup.
		 * Switching revisions retires the outgoing runtime's submissions; reusing that same runtime preserves
		 * frame-slot overlap. Requires closed edits and no outstanding staged execution lock.
		 */
		[[nodiscard]] FArdaRHIStatus Compile(bool bForceRecompile = false);
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
		FArdaGraphNodeHandle FindNode(const eastl::string& Name) const;
		/** Returns a node-declared output, or an invalid handle for an unknown node/slot.
		 * Resolve newly attached outputs before Compile, which may select another cached topology's node handles.
		 */
		FArdaDependencyResourceHandle FindOutput(FArdaGraphNodeHandle Node, const eastl::string& Name) const;
		const FArdaDependencyTopology& GetTopology() const;
		const FArdaInductorCompileResult& GetCompileResult() const;
		/** Snapshot of this graph's PSO cache; all counters are zero before its first pipeline compilation. */
		FArdaPipelineStateCacheStats GetPipelineCacheStats() const;
		/** CUDA capture/replay counters aggregated across the current compiled frame slots. */
		[[nodiscard]] FArdaCudaGraphStats GetCudaGraphStats() const;
		/** Materialize the pending plan on first submission, then submit a frame.
		 * Preparation failure returns a diagnostic without publishing a frame and retains the plan for retry.
		 * Later submissions reuse the runtime, waiting only when recycling an occupied frame slot.
		 * Requires a successful Compile and no pending construction. Attempting submission unlocks editing,
		 * including on submission failure; another submission without new edits retries the selected revision.
		 */
		TArdaRHIResult<FArdaDependencyFrameTicket> Submit(const FArdaGraphExecuteOptions& Options = {});
		FArdaGraphExecutionResult Wait(const FArdaDependencyFrameTicket& Ticket);
		TArdaRHIResult<bool> IsComplete(const FArdaDependencyFrameTicket& Ticket) const;
		/** Submit followed by Wait; unlocks the next construction. Use Submit for overlapping frames. */
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
		/** Open the next transaction. A new post-execution construction starts empty by default.
		 * Pass true only for editor-style changes retaining the previous persistent structure and handles.
		 * Additional edits before Compile extend the queued construction. Compiled graphs remain locked
		 * until Execute/Submit is attempted; authoring never mutates a persistent revision.
		 */
		FArdaRHIStatus BeginGraphEdit(bool bExtendPersistent = false);
		/** Close the open transaction and append its semantic hash and declarations to the ordered edit queue.
		 * Does not compile or submit GPU work. Additional edits extend this construction until Compile succeeds.
		 * Execute is unavailable while editing or while queued changes await compilation.
		 * Requires BeginGraphEdit and an unlocked queue, with external synchronization.
		 */
		FArdaRHIStatus EndGraphEdit();
		/** Cancel only the active transaction, restoring its starting declarations and options.
		 * Earlier queued edits remain queued. Canceling the only edit restores the selected persistent revision.
		 * Does not allocate or rebuild native resources; use DiscardPendingEdits to discard closed edits.
		 * Requires an open edit; returns InvalidState during compilation or without an edit.
		 */
		FArdaRHIStatus CancelGraphEdit();
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
			static_assert(eastl::is_base_of_v<FArdaDependencyNodeBase, Node>,
			    "Typed graph nodes must derive from TArdaDependencyNode or a domain specialization.");
			using FArdaBase = typename Node::FArdaNodeBase;
			static_assert(eastl::is_same_v<typename FArdaBase::FArdaNodeType, Node>,
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
		    eastl::enable_if_t<eastl::is_invocable_v<Initializer, typename Node::FArdaParameters&>, int> = 0>
		TArdaRHIResult<FArdaGraphNodeHandle> AttachOrFind(eastl::string Name, Initializer&& Initialize)
		{
			static_assert(eastl::is_base_of_v<FArdaDependencyNodeBase, Node>,
			    "Typed graph nodes must derive from TArdaDependencyNode or a domain specialization.");
			using FArdaBase = typename Node::FArdaNodeBase;
			static_assert(eastl::is_same_v<typename FArdaBase::FArdaNodeType, Node>,
			    "The node base must name its derived class.");
			return FArdaBase::Attach(*this, eastl::move(Name), eastl::forward<Initializer>(Initialize));
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

		struct FArdaRevisionState;
		friend class FArdaGraphEdit;
		friend class FArdaGraphAccess;
		template <class, class, EArdaDependencyNodeKind>
		friend class TArdaDependencyNode;
		friend class FArdaInductor;
		friend class FArdaGraphRule;
		friend class FArdaDependencyResourceContext;
		TArdaRHIResult<FArdaGraphNodeHandle> AttachErased(eastl::string Name,
		    const eastl::string& Definition,
		    const void* Type,
		    eastl::shared_ptr<const void> Parameters);
		eastl::shared_ptr<FArdaImpl> mImpl;
		eastl::unique_ptr<FArdaRevisionState> mRevisions;
	};
}
