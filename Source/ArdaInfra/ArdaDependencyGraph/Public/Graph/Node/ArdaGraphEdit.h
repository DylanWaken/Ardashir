/** Scoped operation recording and shared node-library invocation. */
#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"
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
	          !eastl::is_invocable_v<First, Parameters&>>
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
		/** Returns a read-only node identity for diagnostics. Use Edit.Order for operation ordering. */
		FArdaGraphNodeHandle GetHandle() const noexcept;
		/** Tests whether invocation attached a node. */
		explicit operator bool() const noexcept;
		/** Returns a declared buffer output; a missing/wrong-type slot fails the edit. */
		FArdaGraphBuffer GetBuffer(const eastl::string& Slot = "Destination") const;
		/** Returns a declared texture output; a missing/wrong-type slot fails the edit. */
		FArdaGraphTexture GetTexture(const eastl::string& Slot = "Destination") const;
		/** Returns a declared acceleration-structure output; invalid slots fail the edit. */
		FArdaGraphAccelerationStructure GetAccelerationStructure(const eastl::string& Slot = "Destination") const;

	private:
		friend class FArdaGraphEdit;
		FArdaGraphOperation(FArdaGraphEdit& Edit, FArdaGraphNodeHandle Node);
		FArdaGraphEdit* mEdit = nullptr;
		FArdaDependencyGraph* mGraph = nullptr;
		FArdaGraphNodeHandle mNode;
	};

	/** Begins an exclusive graph edit and records operations in call order.
	 * The first authoring failure is retained; later operations do nothing and Commit returns that failure.
	 * Cancel and start a new scope to recover. Destruction cancels an uncommitted transaction, preserving
	 * the previous executable graph. Never manually end/reopen its transaction while a scope is active.
	 * Commit only queues declarations; call Graph.Compile after closing all edits before execution.
	 * Graph must outlive this scope. Edits follow the graph's externally synchronized single-owner contract.
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
		/** Restore this transaction's starting declarations, preserving earlier queued edits and cached revisions. */
		FArdaRHIStatus Cancel();
		/** Returns the first recorded failure, including transaction begin/close failures. */
		const FArdaRHIStatus& GetStatus() const noexcept;
		/** Tests whether this scope still owns the graph's current edit. */
		bool IsOpen() const noexcept;
		/** Checks scope ownership and status; extension operations must call before any mutation. */
		bool CanRecord();
		/** Retains the first failure for Commit; successful statuses leave it unchanged. */
		void ReportError(FArdaRHIStatus Status);
		/** Read graph descriptions and device capabilities while composing operations. */
		const FArdaDependencyGraph& GetGraph() const noexcept;
		/** Configure this construction's compiler options; failures are retained for Commit. */
		FArdaRHIStatus SetOptions(const FArdaInductorOptions& Options);
		/** Order two operations when resource dependencies do not express the required sequencing.
		 * Both must belong to this graph's current construction. Failures are retained for Commit.
		 */
		FArdaRHIStatus Order(const FArdaGraphOperation& Producer, const FArdaGraphOperation& Consumer);
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
			auto Result = mGraph.AttachOrFind<Node>(eastl::move(Name), eastl::move(Parameters));
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
			auto Result = mGraph.AttachOrFind(eastl::move(Name), Definition, eastl::move(Value));
			ReportError(Result.mStatus);
			return {*this, Result ? Result.mValue : FArdaGraphNodeHandle{}};
		}

	private:
		friend class FArdaGraphAccess;
		friend class FArdaGraphOperation;
		eastl::string ResolveOperationName(const eastl::string& Prefix, eastl::string Name);
		bool ValidateResource(FArdaDependencyResourceHandle Resource, EArdaGraphResourceKind Kind);
		FArdaDependencyGraph& mGraph;
		FArdaRHIStatus mStatus;
		uint64_t mGeneration = 0;
		bool mbOwnsEdit = false;
	};
}
