/** Scoped operation recording and shared node-library invocation. */
#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"
#include "Graph/Node/ArdaNodeArray.h"
#include "Graph/Resources/ArdaGraphValue.h"
#include <EASTL/functional.h>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>

namespace arda
{
	class FArdaGraphEdit;

	/** Internal overload guard: parameter values and initializer callbacks retain their named forms. */
	template <class Parameters, class... Arguments>
	struct TArdaGraphPositionalArguments : eastl::true_type
	{
	};

	template <class Parameters, class First, class... Arguments>
	struct TArdaGraphPositionalArguments<Parameters, First, Arguments...>
	    : eastl::bool_constant<!eastl::is_same_v<eastl::decay_t<First>, Parameters> &&
	          !eastl::is_invocable_v<First, Parameters&> && !ArdaHasNodeArray<First, Arguments...>>
	{
	};

	/** Outputs of one operation invocation. Retrieve values before closing the edit and compiling.
	 * Retrieved resource values remain usable across queued edits and selection of that construction's
	 * persistent revision. They are not inputs to the next frame's empty construction.
	 */
	class FArdaGraphOperation final
	{
	public:
		/** Constructs an empty operation. */
		FArdaGraphOperation() = default;
		/** Returns a read-only node identity for diagnostics. Stage connections use typed values. */
		FArdaGraphNodeHandle GetHandle() const noexcept;
		/** Tests whether invocation attached a node. */
		explicit operator bool() const noexcept;
		/** Returns a declared buffer output; a missing/wrong-type slot fails the edit. */
		FArdaGraphBuffer GetBuffer(const eastl::string& Slot = "Destination") const;
		/** Returns a declared texture output; a missing/wrong-type slot fails the edit. */
		FArdaGraphTexture GetTexture(const eastl::string& Slot = "Destination") const;
		/** Returns a declared acceleration-structure output; invalid slots fail the edit. */
		FArdaGraphAccelerationStructure GetAccelerationStructure(const eastl::string& Slot = "Destination") const;
		/** Typed logical stage outputs; a wrong stage fails the edit. */
		FArdaGraphVertices GetVertices() const;
		FArdaGraphFragments GetFragments() const;
		FArdaGraphPixels GetPixels() const;
		FArdaGraphControlPoints GetControlPoints() const;
		FArdaGraphMeshPayload GetMeshPayload() const;
		TArdaGraphStageValue<EArdaGraphStageValueKind::RayShader> GetRayShader() const;

	private:
		friend class FArdaGraphEdit;
		FArdaGraphOperation(FArdaGraphEdit& Edit, FArdaGraphNodeHandle Node);
		FArdaGraphEdit* mEdit = nullptr;
		FArdaDependencyGraph* mGraph = nullptr;
		FArdaGraphNodeHandle mNode;
	};

	/** Begins a private graph edit and records operations in call order.
	 * The first authoring failure is retained; later operations do nothing and Commit returns that failure.
	 * Cancel and start a new scope to recover. Destruction cancels an uncommitted transaction, preserving
	 * the previous executable graph. Never manually end/reopen its transaction while a scope is active.
	 * Commit only queues declarations; call Graph.Compile after closing all edits before execution.
	 * Graph must outlive this scope. Distinct threads may own independent scopes concurrently; each scope
	 * itself stays in its opening thread through closure. Recording is private;
	 * only publication is serialized. A scope sees prior
	 * committed edits, never another scope's uncommitted declarations. Concurrent explicit operation-name,
	 * unversioned import or option collisions fail Commit atomically. Versioned imports share one logical
	 * identity for the same physical storage; SetResourceRevision orders their edits before compilation.
	 */
	class FArdaGraphEdit final
	{
	public:
		/** Opens an operation transaction, retaining a failure without taking ownership.
		 * New frame constructions start empty; further queued scopes extend the current construction.
		 */
		explicit FArdaGraphEdit(FArdaDependencyGraph& Graph);
		/** Rolls back this scope's transaction if it was not successfully committed or canceled. */
		~FArdaGraphEdit();
		FArdaGraphEdit(const FArdaGraphEdit&) = delete;
		FArdaGraphEdit& operator=(const FArdaGraphEdit&) = delete;
		/** Close and queue a successful edit without compiling. Call Graph.Compile before execution.
		 * An authoring failure remains open until Cancel or destruction and is never queued.
		 */
		[[nodiscard]] FArdaRHIStatus Commit();
		/** Discard private declarations and state, preserving other scopes, queued edits and cached revisions. */
		FArdaRHIStatus Cancel();
		/** Returns the first recorded failure, including transaction begin/close failures. */
		const FArdaRHIStatus& GetStatus() const noexcept;
		/** Tests whether this scope still owns an open private edit. */
		bool IsOpen() const noexcept;
		/** Checks scope ownership and status; extension operations must call before any mutation. */
		bool CanRecord();
		/** Retains the first failure for Commit; successful statuses leave it unchanged. */
		void ReportError(FArdaRHIStatus Status);
		/** Read this scope's private construction snapshot and device capabilities while composing operations. */
		const FArdaDependencyGraph& GetGraph() const noexcept;
		/** Configure this construction's compiler options; failures are retained for Commit. */
		FArdaRHIStatus SetOptions(const FArdaInductorOptions& Options);
		/** Order two operations when resource dependencies do not express the required sequencing.
		 * Both must belong to this graph's current construction. Failures are retained for Commit.
		 */
		FArdaRHIStatus Order(const FArdaGraphOperation& Producer, const FArdaGraphOperation& Consumer);
		/** Explicitly invoke the producer's registered materializer. Never allocates an implicit stage buffer.
		 * A stage without a materializer returns Unsupported; its shader library must define a capture layout.
		 */
		FArdaGraphBuffer FetchBuffer(FArdaGraphStageValue Value, const FArdaStageBufferRequest& Request = {});
		/** Retrieve a physical output explicitly declared by a stage, such as a pixel capture buffer. */
		FArdaGraphBuffer GetStageBuffer(FArdaGraphStageValue Value, const eastl::string& Slot = "Destination");
		/** Produce a fresh operation name within this construction; equivalent constructions repeat names. */
		eastl::string MakeName(const eastl::string& Prefix);
		/** Validates graph ownership, generation and buffer kind before constructing a typed value. */
		FArdaGraphBuffer UseBuffer(FArdaDependencyResourceHandle Resource);
		/** Validates graph ownership, generation and texture kind before constructing a typed value. */
		FArdaGraphTexture UseTexture(FArdaDependencyResourceHandle Resource);
		/** Validates graph ownership, generation and acceleration-structure kind. */
		FArdaGraphAccelerationStructure UseAccelerationStructure(FArdaDependencyResourceHandle Resource);
		/** Retains a resource as a graph output so its producers survive culling. */
		void Keep(FArdaDependencyResourceHandle Resource);
		/** Give this edit's accesses to shared storage an explicit ordering version.
		 * Smaller versions precede larger versions regardless of commit order; ties retain commit order.
		 * Unversioned accesses retain their position relative to other accesses. A cycle between resource
		 * versions or explicit dependencies terminates compilation with an always-on fatal diagnostic.
		 * One edit may name each resource only once (repeating the same version is harmless).
		 */
		FArdaRHIStatus SetResourceRevision(FArdaDependencyResourceHandle Resource, uint64_t Revision);

		/** Shared extension path: register and invoke a node class using a fresh operation identity.
		 * All existing requirement checks, resource declarations and immutable parameter snapshots apply.
		 */
		template <class Node>
		FArdaGraphOperation Invoke(typename Node::FArdaParameters Parameters, eastl::string Name = {})
		{
			if (!CanRecord())
			{
				return {*this, {}};
			}
			Name = ResolveOperationName(Node::GetMetadata().mName, eastl::move(Name));
			if (!mStatus)
			{
				return {*this, {}};
			}
			auto Result = GetRecordingGraph().AttachOrFind<Node>(eastl::move(Name), eastl::move(Parameters));
			ReportError(Result.mStatus);
			return {*this, Result ? Result.mValue : FArdaGraphNodeHandle{}};
		}

		/** Initialize a fresh parameter value once, then record it. Initializers return void or status;
		 * do not retain their parameter reference or mutate the edit from the callback.
		 */
		template <class Node,
		    class Initializer,
		    eastl::enable_if_t<eastl::is_invocable_v<Initializer, typename Node::FArdaParameters&>, int> = 0>
		FArdaGraphOperation Invoke(Initializer&& Initialize, eastl::string Name = {})
		{
			using FArdaResult = eastl::invoke_result_t<Initializer, typename Node::FArdaParameters&>;
			static_assert(eastl::is_same_v<FArdaResult, void> || eastl::is_same_v<FArdaResult, FArdaRHIStatus>,
			    "A node parameter initializer must return void or FArdaRHIStatus.");
			if (!CanRecord())
			{
				return {*this, {}};
			}
			typename Node::FArdaParameters Parameters{};
			if constexpr (eastl::is_same_v<FArdaResult, FArdaRHIStatus>)
			{
				ReportError(eastl::invoke(eastl::forward<Initializer>(Initialize), Parameters));
			}
			else
			{
				eastl::invoke(eastl::forward<Initializer>(Initialize), Parameters);
			}
			return Invoke<Node>(eastl::move(Parameters), eastl::move(Name));
		}

		/** Record a node from positional fields using checked brace initialization of its parameters.
		 * Omitted fields use their declared defaults. Invalid types and narrowing fail at compile time.
		 * Use the parameter-value or initializer overload when supplying a diagnostic name.
		 */
		template <class Node,
		    class... Arguments,
		    eastl::enable_if_t<TArdaGraphPositionalArguments<typename Node::FArdaParameters, Arguments...>::value,
		        int> = 0>
		auto Invoke(Arguments&&... Values)
		    -> decltype(typename Node::FArdaParameters{eastl::forward<Arguments>(Values)...}, FArdaGraphOperation{})
		{
			if (!CanRecord())
			{
				return {*this, {}};
			}
			return Invoke<Node>(typename Node::FArdaParameters{eastl::forward<Arguments>(Values)...});
		}

		/** Record an array of complete parameter snapshots. Each result is a distinct logical operation;
		 * the compiler receives the shared invocation family and batches compatible execution work.
		 * Validation failures poison this edit just like scalar calls; Commit publishes no partial family.
		 */
		template <class Node>
		TArdaNodeArray<FArdaGraphOperation> Invoke(const TArdaNodeArray<typename Node::FArdaParameters>& Values,
		    eastl::string Name = {})
		{
			TArdaNodeArray<FArdaGraphOperation> Result;
			if (!CanRecord())
			{
				return Result;
			}
			Result.reserve(Values.size());
			for (const auto& Value : Values)
			{
				auto Operation = Invoke<Node>(Value,
				    Name.empty() ? eastl::string{} : Name + "[" + eastl::to_string(Result.size()) + "]");
				if (!Operation)
				{
					return {};
				}
				Result.push_back(eastl::move(Operation));
			}
			MarkArrayInvocation(Result);
			return Result;
		}

		/** Zip array-valued slots and broadcast scalar slots through the node's parameter constructor.
		 * All lengths are checked before attachment; an empty array paired with a nonempty array is an
		 * error. Scalars must be reusable values: move-only values cannot be broadcast to multiple lanes.
		 */
		template <class Node, class... Arguments, eastl::enable_if_t<ArdaHasNodeArray<Arguments...>, int> = 0>
		auto Invoke(Arguments&&... Values)
		    -> decltype(typename Node::FArdaParameters{node_array_detail::At(Values, size_t{})...},
		        TArdaNodeArray<FArdaGraphOperation>{})
		{
			if (!CanRecord())
			{
				return {};
			}
			size_t Count = 0;
			if (!node_array_detail::ResolveCount(Count, Values...))
			{
				ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Node argument arrays must have identical lengths; scalar parameters broadcast."));
				return {};
			}
			TArdaNodeArray<typename Node::FArdaParameters> Parameters;
			Parameters.reserve(Count);
			for (size_t Index = 0; Index < Count; ++Index)
			{
				Parameters.push_back(typename Node::FArdaParameters{node_array_detail::At(Values, Index)...});
			}
			return Invoke<Node>(static_cast<const TArdaNodeArray<typename Node::FArdaParameters>&>(Parameters));
		}

		/** Invokes an already registered definition by its library name and exact public parameter type.
		 * Uses the same validation and fresh identity policy as class invocation; missing definitions fail.
		 */
		template <class Parameters>
		FArdaGraphOperation Invoke(const eastl::string& Definition, Parameters Value, eastl::string Name = {})
		{
			if (!CanRecord())
			{
				return {*this, {}};
			}
			Name = ResolveOperationName(Definition, eastl::move(Name));
			if (!mStatus)
			{
				return {*this, {}};
			}
			auto Result = GetRecordingGraph().AttachOrFind(eastl::move(Name), Definition, eastl::move(Value));
			ReportError(Result.mStatus);
			return {*this, Result ? Result.mValue : FArdaGraphNodeHandle{}};
		}

		/** Invoke a retained immutable registry definition through the ordinary attachment contract.
		 * Later registry removal or replacement does not change this library's implementation.
		 */
		template <class Parameters>
		FArdaGraphOperation Invoke(const eastl::shared_ptr<const FArdaDependencyNodeExecutable>& Definition,
		    Parameters Value,
		    eastl::string Name = {})
		{
			if (!CanRecord())
			{
				return {*this, {}};
			}
			if (!Definition)
			{
				ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Invocation requires a retained definition."));
				return {*this, {}};
			}
			Name = ResolveOperationName(Definition->mName, eastl::move(Name));
			if (!mStatus)
			{
				return {*this, {}};
			}
			auto Result = GetRecordingGraph().AttachErased(eastl::move(Name),
			    Definition->mName,
			    &ArdaDependencyParameterType<Parameters>,
			    eastl::make_shared<const Parameters>(eastl::move(Value)),
			    Definition);
			ReportError(Result.mStatus);
			return {*this, Result ? Result.mValue : FArdaGraphNodeHandle{}};
		}

		/** Invoke complete parameter lanes by registry identity. A diagnostic prefix names indexed lanes. */
		template <class Parameters>
		TArdaNodeArray<FArdaGraphOperation> Invoke(const eastl::string& Definition,
		    const TArdaNodeArray<Parameters>& Values,
		    eastl::string Name = {})
		{
			TArdaNodeArray<FArdaGraphOperation> Result;
			if (!CanRecord())
			{
				return Result;
			}
			Result.reserve(Values.size());
			for (const auto& Value : Values)
			{
				auto Operation = Invoke(Definition,
				    Value,
				    Name.empty() ? eastl::string{} : Name + "[" + eastl::to_string(Result.size()) + "]");
				if (!Operation)
				{
					return {};
				}
				Result.push_back(eastl::move(Operation));
			}
			MarkArrayInvocation(Result);
			return Result;
		}

	private:
		friend class FArdaDependencyGraph;
		friend class FArdaGraphAccess;
		friend class FArdaGraphOperation;
		friend class FArdaSubgraphPreprocessor;
		friend class FArdaInductorGraph;
		FArdaGraphEdit(FArdaDependencyGraph& Graph, eastl::string Prefix, bool bSubgraphContent = true);
		void MarkArrayInvocation(const TArdaNodeArray<FArdaGraphOperation>& Operations);
		eastl::string ResolveOperationName(const eastl::string& Prefix, eastl::string Name);
		bool ValidateResource(FArdaDependencyResourceHandle Resource, EArdaGraphResourceKind Kind);
		bool ValidateStage(FArdaGraphStageValue Value);
		FArdaDependencyGraph& GetRecordingGraph() const noexcept;
		FArdaDependencyGraph& mGraph;
		eastl::unique_ptr<FArdaDependencyGraph> mRecordingGraph;
		eastl::shared_ptr<const FArdaDependencyGraph::FArdaImpl> mBase;
		uint64_t mOptionsVersion = 0;
		bool mbOptionsChanged = false;
		FArdaRHIStatus mStatus;
		uint64_t mGeneration = 0;
		bool mbOwnsEdit = false;
		bool mbSubgraphContent = false;
		bool mbPreprocessContent = false;
		eastl::string mNamePrefix;
		size_t mFirstSubgraphResource = 0;
		uint64_t mFirstSubgraphAttachmentOrder = 0;
		eastl::vector<FArdaDependencyAccess> mSubgraphAccesses;
		eastl::vector<FArdaGraphResourceRevision> mResourceRevisions;
	};
}
