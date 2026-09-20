#pragma once
#include "Graph/Node/ArdaDependencyGraph.h"
#include "Graph/Node/ArdaGraphEdit.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "RHI/Scheduling/ArdaCudaSequence.h"
#include "Graph/Node/ArdaDependencyKey.h"
#include <EASTL/functional.h>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>

namespace arda
{
	/** Stable registry identity supplied by a node class; the base specialization fixes its execution domain. */
	struct FArdaDependencyNodeMetadata
	{
		/** Nonempty process-wide definition name; must remain constant for this class. */
		eastl::string mName;
		/** Immutable implementation revision. A conflicting registered revision is rejected. */
		uint32_t mVersion = 1;
	};

	/** Default state for nodes with no device preparation or private per-instance state. */
	struct FArdaEmptyDependencyNodeState
	{
	};

	/** Common root of all class-authored nodes. Contains no data or virtual dispatch. */
	class FArdaDependencyNodeBase
	{
	protected:
		FArdaDependencyNodeBase() = default;
		template <class Node, class>
		friend struct TArdaDependencyNodeContract;

		template <class Node, class = void>
		struct TArdaNodeRecordHook : eastl::false_type
		{
		};

		template <class Node>
		struct TArdaNodeRecordHook<Node,
		    eastl::void_t<decltype(Node::Record(eastl::declval<FArdaDependencyExecutionContext&>(),
		        eastl::declval<const typename Node::FArdaParameters&>(),
		        eastl::declval<const typename Node::FArdaState&>(),
		        eastl::declval<typename Node::FArdaInstanceState&>()))>>
		    : eastl::is_same<FArdaRHIStatus,
		          decltype(Node::Record(eastl::declval<FArdaDependencyExecutionContext&>(),
		              eastl::declval<const typename Node::FArdaParameters&>(),
		              eastl::declval<const typename Node::FArdaState&>(),
		              eastl::declval<typename Node::FArdaInstanceState&>()))>
		{
		};

		template <class Node, class = void>
		struct TArdaNodeCudaHook : eastl::false_type
		{
		};

		template <class Node>
		struct TArdaNodeCudaHook<Node,
		    eastl::void_t<decltype(Node::PrepareCuda(eastl::declval<FArdaDependencyExecutionContext&>(),
		        eastl::declval<const typename Node::FArdaParameters&>(),
		        eastl::declval<const typename Node::FArdaState&>(),
		        eastl::declval<typename Node::FArdaInstanceState&>(),
		        eastl::declval<FArdaCudaSequence&>()))>>
		    : eastl::is_same<FArdaRHIStatus,
		          decltype(Node::PrepareCuda(eastl::declval<FArdaDependencyExecutionContext&>(),
		              eastl::declval<const typename Node::FArdaParameters&>(),
		              eastl::declval<const typename Node::FArdaState&>(),
		              eastl::declval<typename Node::FArdaInstanceState&>(),
		              eastl::declval<FArdaCudaSequence&>()))>
		{
		};
	};

	/** Compile-time check for metadata, identity, description, preparation and the selected execution hook.
	 * GetRequirements, Prepare, Validate and CreateInstance have stateless defaults in the base. Record is required for native
	 * work; PrepareCuda is required for CUDA. Synchronization nodes may omit execution entirely.
	 */
	template <class Node, class = void>
	struct TArdaDependencyNodeContract : eastl::false_type
	{
	};

	template <class Node>
	struct TArdaDependencyNodeContract<Node,
	    eastl::void_t<typename Node::FArdaNodeBase,
	        decltype(Node::GetMetadata()),
	        decltype(Node::GetRequirements(eastl::declval<const typename Node::FArdaParameters&>())),
	        decltype(Node::DeclareResources(eastl::declval<FArdaDependencyResourceContext&>(),
	            eastl::declval<typename Node::FArdaParameters&>())),
	        decltype(Node::GetCanonicalKey(eastl::declval<const typename Node::FArdaParameters&>())),
	        decltype(Node::Describe(eastl::declval<const typename Node::FArdaParameters&>(),
	            eastl::declval<const typename Node::FArdaState&>())),
	        decltype(Node::Prepare(eastl::declval<FArdaRHIDeviceRef>())),
	        decltype(Node::Validate(eastl::declval<const typename Node::FArdaParameters&>())),
	        decltype(Node::CreateInstance(eastl::declval<FArdaRHIDeviceRef>(),
	            eastl::declval<const typename Node::FArdaParameters&>(),
	            eastl::declval<const typename Node::FArdaState&>()))>>
	    : eastl::bool_constant<eastl::is_base_of_v<FArdaDependencyNodeBase, Node> &&
	          eastl::is_same_v<typename Node::FArdaNodeBase::FArdaNodeType, Node> &&
	          eastl::is_same_v<decltype(Node::GetMetadata()), FArdaDependencyNodeMetadata> &&
	          eastl::is_same_v<decltype(Node::GetRequirements(eastl::declval<const typename Node::FArdaParameters&>())),
	              FArdaDependencyNodeRequirements> &&
	          eastl::is_same_v<decltype(Node::DeclareResources(eastl::declval<FArdaDependencyResourceContext&>(),
	                               eastl::declval<typename Node::FArdaParameters&>())),
	              FArdaRHIStatus> &&
	          eastl::is_same_v<decltype(Node::GetCanonicalKey(eastl::declval<const typename Node::FArdaParameters&>())),
	              eastl::string> &&
	          eastl::is_same_v<decltype(Node::Describe(eastl::declval<const typename Node::FArdaParameters&>(),
	                               eastl::declval<const typename Node::FArdaState&>())),
	              FArdaDependencyNodeDesc> &&
	          eastl::is_same_v<decltype(Node::Prepare(eastl::declval<FArdaRHIDeviceRef>())),
	              TArdaRHIResult<eastl::shared_ptr<const typename Node::FArdaState>>> &&
	          eastl::is_same_v<decltype(Node::Validate(eastl::declval<const typename Node::FArdaParameters&>())),
	              FArdaRHIStatus> &&
	          eastl::is_same_v<decltype(Node::CreateInstance(eastl::declval<FArdaRHIDeviceRef>(),
	                               eastl::declval<const typename Node::FArdaParameters&>(),
	                               eastl::declval<const typename Node::FArdaState&>())),
	              TArdaRHIResult<eastl::shared_ptr<typename Node::FArdaInstanceState>>> &&
	          (Node::mKind == EArdaDependencyNodeKind::Cuda ? FArdaDependencyNodeBase::TArdaNodeCudaHook<Node>::value &&
	                      !FArdaDependencyNodeBase::TArdaNodeRecordHook<Node>::value
	                                                        : !FArdaDependencyNodeBase::TArdaNodeCudaHook<
	                                                              Node>::value &&
	                      (FArdaDependencyNodeBase::TArdaNodeRecordHook<Node>::value ||
	                          Node::mKind == EArdaDependencyNodeKind::Synchronization))>
	{
	};

	/** Master CRTP node contract, deriving from FArdaDependencyNodeBase without virtual dispatch.
	 * Derived supplies static GetMetadata() -> FArdaDependencyNodeMetadata,
	 * GetCanonicalKey(const FArdaParameters&) -> eastl::string and
	 * Describe(const FArdaParameters&, const FArdaState&) -> FArdaDependencyNodeDesc.
	 * Native nodes implement Record(Context&, const FArdaParameters&, const FArdaState&, FArdaInstanceState&)
	 * returning FArdaRHIStatus. CUDA nodes instead implement PrepareCuda with the same arguments plus
	 * FArdaCudaSequence& and append operations without submitting. Synchronization may omit execution.
	 * GetMetadata must be stable; canonical keys cover every behavior-affecting parameter and full resource
	 * identity without structure padding. Describe must declare all resource accesses and pipeline needs.
	 * Override FArdaState/Prepare for immutable device setup and FArdaInstanceState/CreateInstance for private
	 * mutable operation state. The graph prepares declared shader bindings and tables. Opaque state types may
	 * be declared in a node header and defined in its .cpp.
	 * Preparation is fixed by node type/device; parameter-dependent setup belongs in CreateInstance and its
	 * memory requirements in Describe. Do not submit GPU work in preparation hooks.
	 * @ownership The base retains immutable parameter snapshots, prepared state and per-instance state.
	 * Nested pointees are not deep-copied; keep semantic inputs immutable and synchronize dynamic inputs.
	 * Weak device-state cache entries do not extend state or device lifetime after the last instance releases them.
	 * @errors Incomplete node contracts fail at registration/attachment compilation. Preparation failures propagate
	 * without insertion; successful null state results return InvalidState. Name collisions are InvalidArgument.
	 * @threading Registration and device preparation are serialized per class. Shared state is immutable.
	 * Graph edits and submission follow the graph's single-owner contract; distinct nodes can record concurrently.
	 * Private operation state may mutate during recording, following declared resource effects and frame lifetimes.
	 */
	template <class Derived, class Parameters, EArdaDependencyNodeKind Kind = EArdaDependencyNodeKind::Compute>
	class TArdaDependencyNode : public FArdaDependencyNodeBase
	{
	public:
		/** Exact CRTP base used by typed graph attachment; do not replace in the derived class. */
		using FArdaNodeBase = TArdaDependencyNode<Derived, Parameters, Kind>;
		/** Concrete class validated by the attachment contract. */
		using FArdaNodeType = Derived;
		/** Node-specific attachment struct. Distinct node classes must declare distinct structs, even for
		 * identical fields; do not alias another node's parameters or use a shared catch-all schema.
		 */
		using FArdaParameters = Parameters;
		/** Default immutable state. Hide with a custom type and implement Prepare(Device) when needed. */
		using FArdaState = FArdaEmptyDependencyNodeState;
		/** Default instance state. Hide with a custom type and implement CreateInstance when needed. */
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;
		/** Execution domain fixed by this specialization; queue selection remains ArdaInductor's responsibility. */
		static constexpr EArdaDependencyNodeKind mKind = Kind;

		/** Override to list hardware, feature and environment requirements for the public parameters.
		 * Runs before DeclareResources, Prepare and CreateInstance, including matching reattachments.
		 * The default requires nothing. The graph also checks recognizable descriptor requirements
		 * after Describe; list features here when they must be checked before shader/device setup.
		 * Do not submit work, allocate resources or mutate external state in this hook.
		 */
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters&)
		{
			return {};
		}

		/** Declare outputs from logical inputs and scalar settings. Empty output handles are filled
		 * by the graph; explicitly supplied handles are checked against the same requirements.
		 * The default declares none. Must not submit GPU work or modify external state.
		 */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext&, FArdaParameters&)
		{
			return {};
		}

		/** Validate public parameters before device setup. Defaults to success. */
		static FArdaRHIStatus Validate(const FArdaParameters&)
		{
			return {};
		}

		/** Stateless default. A derived class declaring FArdaState must provide its own Prepare(Device). */
		template <class Node = Derived,
		    eastl::enable_if_t<eastl::is_same_v<typename Node::FArdaState, FArdaEmptyDependencyNodeState>, int> = 0>
		static TArdaRHIResult<eastl::shared_ptr<const typename Node::FArdaState>> Prepare(FArdaRHIDeviceRef)
		{
			static_assert(eastl::is_same_v<typename Node::FArdaState, FArdaEmptyDependencyNodeState>,
			    "Nodes with custom FArdaState must implement Prepare(Device).");
			return {eastl::make_shared<const FArdaEmptyDependencyNodeState>(), {}};
		}

		/** Stateless default. A custom FArdaInstanceState requires CreateInstance(Device, Parameters, State). */
		template <class Node = Derived,
		    eastl::enable_if_t<eastl::is_same_v<typename Node::FArdaInstanceState, FArdaEmptyDependencyNodeState>,
		        int> = 0>
		static TArdaRHIResult<eastl::shared_ptr<typename Node::FArdaInstanceState>> CreateInstance(FArdaRHIDeviceRef,
		    const FArdaParameters&,
		    const typename Node::FArdaState&)
		{
			static_assert(eastl::is_same_v<typename Node::FArdaInstanceState, FArdaEmptyDependencyNodeState>,
			    "Nodes with custom FArdaInstanceState must implement CreateInstance(Device, Parameters, State).");
			return {eastl::make_shared<FArdaEmptyDependencyNodeState>(), {}};
		}

		/** Registers a device-independent definition, or finds this class's matching registration.
		 * Unregistering permits later re-registration; another implementation/version under the same name fails.
		 * The explicit registry overload also supports built-in bootstrap without recursive singleton lookup.
		 */
		static FArdaRHIStatus Register(FArdaNodeRegistry& Registry)
		{
			static_assert(TArdaDependencyNodeContract<Derived>::value,
			    "Node must implement GetMetadata, GetCanonicalKey, Describe and its domain's execution hook; inherited or overridden requirements/preparation hooks must match the base signatures.");
			static_assert(Derived::mKind == Kind, "A node's execution domain is fixed by its base specialization.");
			static std::mutex Mutex;
			std::lock_guard<std::mutex> Lock(Mutex);
			const auto Metadata = Derived::GetMetadata();
			using FArdaBound = TArdaBoundParameters<Derived>;
			if (const auto Existing = Registry.Find(Metadata.mName))
			{
				if (Existing->mPreparedParameterType == &ArdaDependencyParameterType<FArdaBound> &&
				    Existing->mParameterType == &ArdaDependencyParameterType<Parameters> &&
				    Existing->mVersion == Metadata.mVersion && Existing->mKind == Kind)
				{
					return {};
				}
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Another node implementation or version owns this registry name.");
			}
			// Lower the checked class contract once. All attachment routes use these same callbacks.
			FArdaDependencyNodeExecutable Definition;
			Definition.mName = Metadata.mName;
			Definition.mVersion = Metadata.mVersion;
			Definition.mKind = Kind;
			Definition.mParameterType = &ArdaDependencyParameterType<Parameters>;
			Definition.mPreparedParameterType = &ArdaDependencyParameterType<FArdaBound>;
			Definition.mGetRequirements = [](const void* Value)
			{
				return Derived::GetRequirements(*static_cast<const Parameters*>(Value));
			};
			Definition.mResolveResources =
			    [](FArdaDependencyResourceContext& Context,
			        eastl::shared_ptr<const void> Value) -> TArdaRHIResult<eastl::shared_ptr<const void>>
			{
				auto Resolved = eastl::make_shared<Parameters>(*eastl::static_pointer_cast<const Parameters>(Value));
				if (auto Status = Derived::DeclareResources(Context, *Resolved); !Status)
				{
					return {{}, eastl::move(Status)};
				}

				return {eastl::move(Resolved), {}};
			};
			Definition.mCanonicalKey = [](const void* Value)
			{
				return Derived::GetCanonicalKey(*static_cast<const Parameters*>(Value));
			};
			Definition.mPrepare =
			    [](FArdaRHIDeviceRef Device,
			        eastl::shared_ptr<const void> Value) -> TArdaRHIResult<eastl::shared_ptr<const void>>
			{
				auto Result = PrepareParameters<Derived>(eastl::move(Device),
				    eastl::static_pointer_cast<const Parameters>(eastl::move(Value)));
				return {eastl::move(Result.mValue), eastl::move(Result.mStatus)};
			};
			Definition.mDescribe = [](const void* Value)
			{
				const auto& P = *static_cast<const FArdaBound*>(Value);
				return Derived::Describe(*P.mParameters, *P.mPrepared->mState);
			};

			// Each domain exposes exactly one recording hook; CUDA work is appended to the shared sequence.
			if constexpr (Kind == EArdaDependencyNodeKind::Cuda)
			{
				Definition.mPrepareCuda =
				    [](FArdaDependencyExecutionContext& Context, const void* Value, FArdaCudaSequence& Sequence)
				{
					const auto& P = *static_cast<const FArdaBound*>(Value);
					return Derived::PrepareCuda(Context, *P.mParameters, *P.mPrepared->mState, *P.mInstance, Sequence);
				};
			}
			else if constexpr (FArdaDependencyNodeBase::TArdaNodeRecordHook<Derived>::value)
			{
				Definition.mRecord = [](FArdaDependencyExecutionContext& Context, const void* Value)
				{
					const auto& P = *static_cast<const FArdaBound*>(Value);
					return Derived::Record(Context, *P.mParameters, *P.mPrepared->mState, *P.mInstance);
				};
			}

			return Registry.RegisterExecutable(eastl::move(Definition));
		}

		/** Register in the global node library without preparing or retaining a device. */
		static FArdaRHIStatus Register()
		{
			return Register(FArdaNodeRegistry::Get());
		}

		/** Records a fresh operation and returns its declared outputs, registering this class on demand.
		 * Errors are retained by Edit and reported by Commit. Repeated invocations remain distinct nodes.
		 * Node libraries may expose domain-specific static functions which build parameters and call Invoke.
		 */
		static FArdaGraphOperation Invoke(FArdaGraphEdit& Edit, FArdaParameters Value, eastl::string Name = {})
		{
			return Edit.Invoke<Derived>(eastl::move(Value), eastl::move(Name));
		}

		/** Fill fresh parameters synchronously, then record one operation. The initializer returns void
		 * or FArdaRHIStatus; failures are retained by the edit. The optional name is diagnostic and unique.
		 */
		template <class Initializer, eastl::enable_if_t<eastl::is_invocable_v<Initializer, FArdaParameters&>, int> = 0>
		static FArdaGraphOperation Invoke(FArdaGraphEdit& Edit, Initializer&& Initialize, eastl::string Name = {})
		{
			return Edit.Invoke<Derived>(eastl::forward<Initializer>(Initialize), eastl::move(Name));
		}

		/** Record positional fields in FArdaParameters declaration order, preserving their value categories.
		 * Brace initialization checks types and narrowing; omitted fields use their defaults.
		 * Parameter-value and initializer overloads retain the optional diagnostic name.
		 */
		template <class... Arguments,
		    eastl::enable_if_t<TArdaGraphPositionalArguments<FArdaParameters, Arguments...>::value, int> = 0>
		static auto Invoke(FArdaGraphEdit& Edit, Arguments&&... Values)
		    -> decltype(FArdaParameters{eastl::forward<Arguments>(Values)...}, FArdaGraphOperation{})
		{
			return Edit.Invoke<Derived>(eastl::forward<Arguments>(Values)...);
		}

		/** A stateless node object records a fresh operation when called with parameters and an optional name. */
		FArdaGraphOperation operator()(FArdaGraphEdit& Edit, FArdaParameters Value, eastl::string Name = {}) const
		{
			return Invoke(Edit, eastl::move(Value), eastl::move(Name));
		}

		/** Callable shorthand for positional fields, default parameters, or a parameter initializer.
		 * The object owns no execution state; the graph retains every invocation independently.
		 */
		template <class... Arguments>
		auto operator()(FArdaGraphEdit& Edit, Arguments&&... Values) const
		    -> decltype(Invoke(Edit, eastl::forward<Arguments>(Values)...))
		{
			return Invoke(Edit, eastl::forward<Arguments>(Values)...);
		}

	private:
		friend class FArdaDependencyGraph;
		friend class FArdaGraphAccess;

		/** Common attachment path. The graph validates identity before preparing a new instance.
		 * Device state is weakly cached per class/device and strongly retained by live bound instances.
		 * Failures are not cached. Instance state is created separately for each new named node.
		 */
		static TArdaRHIResult<FArdaGraphNodeHandle> Attach(FArdaDependencyGraph& Graph,
		    eastl::string Name,
		    FArdaParameters P)
		{
			if (!Graph.IsEditing())
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Attach nodes inside a graph edit.")};
			}
			if (auto Status = Register(); !Status)
			{
				return {{}, eastl::move(Status)};
			}
			return Graph.AttachOrFind(eastl::move(Name), Derived::GetMetadata().mName, eastl::move(P));
		}

		/** Initialize a fresh parameter struct synchronously inside an edit, then attach its frozen value.
		 * Initialize accepts FArdaParameters& and returns void or FArdaRHIStatus; failure prevents attachment.
		 * Runs once per call, including reattachment. Do not retain the reference or mutate Graph in the callback.
		 */
		template <class Initializer, eastl::enable_if_t<eastl::is_invocable_v<Initializer, FArdaParameters&>, int> = 0>
		static TArdaRHIResult<FArdaGraphNodeHandle> Attach(FArdaDependencyGraph& Graph,
		    eastl::string Name,
		    Initializer&& Initialize)
		{
			using FArdaResult = eastl::invoke_result_t<Initializer, FArdaParameters&>;
			static_assert(eastl::is_same_v<FArdaResult, void> || eastl::is_same_v<FArdaResult, FArdaRHIStatus>,
			    "A node parameter initializer must return void or FArdaRHIStatus.");
			if (!Graph.IsEditing())
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Attach nodes inside a graph edit.")};
			}
			FArdaParameters P{};
			if constexpr (eastl::is_same_v<FArdaResult, FArdaRHIStatus>)
			{
				if (auto Status = eastl::invoke(eastl::forward<Initializer>(Initialize), P); !Status)
				{
					return {{}, eastl::move(Status)};
				}
			}
			else
			{
				eastl::invoke(eastl::forward<Initializer>(Initialize), P);
			}
			return Attach(Graph, eastl::move(Name), eastl::move(P));
		}

	private:
		template <class Node>
		struct TArdaPreparedState
		{
			FArdaRHIDeviceRef mDevice;
			eastl::shared_ptr<const typename Node::FArdaState> mState;
		};

		template <class Node>
		struct TArdaBoundParameters
		{
			eastl::shared_ptr<const Parameters> mParameters;
			eastl::shared_ptr<const TArdaPreparedState<Node>> mPrepared;
			eastl::shared_ptr<typename Node::FArdaInstanceState> mInstance;
		};

		template <class Node>
		static TArdaRHIResult<eastl::shared_ptr<const TArdaPreparedState<Node>>> GetPreparedState(
		    FArdaRHIDeviceRef Device)
		{
			static std::mutex Mutex;
			static eastl::vector<eastl::weak_ptr<const TArdaPreparedState<Node>>> States;
			std::lock_guard<std::mutex> Lock(Mutex);
			for (auto It = States.begin(); It != States.end();)
			{
				if (auto Existing = It->lock())
				{
					if (Existing->mDevice == Device)
					{
						return {eastl::move(Existing), {}};
					}
					++It;
				}
				else
				{
					It = States.erase(It);
				}
			}
			auto State = Node::Prepare(Device);
			if (!State)
			{
				return {{}, eastl::move(State.mStatus)};
			}
			if (!State.mValue)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Prepare returned no node state.")};
			}
			// Construct the owner before exposing immutable shared state.
			auto Owner = eastl::make_shared<TArdaPreparedState<Node>>();
			Owner->mDevice = eastl::move(Device);
			Owner->mState = eastl::move(State.mValue);
			eastl::shared_ptr<const TArdaPreparedState<Node>> Prepared = eastl::move(Owner);
			States.push_back(Prepared);
			return {eastl::move(Prepared), {}};
		}

		template <class Node>
		static TArdaRHIResult<eastl::shared_ptr<const TArdaBoundParameters<Node>>> PrepareParameters(
		    FArdaRHIDeviceRef Device,
		    eastl::shared_ptr<const Parameters> P)
		{
			if (auto Status = Node::Validate(*P); !Status)
			{
				return {{}, eastl::move(Status)};
			}
			auto Prepared = GetPreparedState<Node>(Device);
			if (!Prepared)
			{
				return {{}, eastl::move(Prepared.mStatus)};
			}
			auto Instance = Node::CreateInstance(Device, *P, *Prepared.mValue->mState);
			if (!Instance)
			{
				return {{}, eastl::move(Instance.mStatus)};
			}
			if (!Instance.mValue)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "CreateInstance returned no instance state.")};
			}
			auto Bound = eastl::make_shared<TArdaBoundParameters<Node>>();
			Bound->mParameters = eastl::move(P);
			Bound->mPrepared = eastl::move(Prepared.mValue);
			Bound->mInstance = eastl::move(Instance.mValue);
			return {eastl::move(Bound), {}};
		}
	};

	/** Graphics nodes implement Record(Context, Parameters, State, Instance). */
	template <class Node, class Parameters>
	using TArdaGraphicsDependencyNode = TArdaDependencyNode<Node, Parameters, EArdaDependencyNodeKind::Graphics>;
	/** Native compute nodes implement Record; ArdaInductor determines whether async compute is appropriate. */
	template <class Node, class Parameters>
	using TArdaComputeDependencyNode = TArdaDependencyNode<Node, Parameters, EArdaDependencyNodeKind::Compute>;
	/** Transfer nodes implement Record and declare all transfer accesses. */
	template <class Node, class Parameters>
	using TArdaCopyDependencyNode = TArdaDependencyNode<Node, Parameters, EArdaDependencyNodeKind::Copy>;
	/** CUDA nodes implement PrepareCuda(Context, Parameters, State, Instance, Sequence), never Record. */
	template <class Node, class Parameters>
	using TArdaCudaDependencyNode = TArdaDependencyNode<Node, Parameters, EArdaDependencyNodeKind::Cuda>;
	/** Synchronization nodes may omit execution and describe only ordering and resource effects. */
	template <class Node, class Parameters>
	using TArdaSynchronizationDependencyNode =
	    TArdaDependencyNode<Node, Parameters, EArdaDependencyNodeKind::Synchronization>;
}

/** Declare a named, immutable callable immediately after its node class at namespace scope.
 * Put the matching ARDA_DEFINE_GRAPH_NODE in exactly one source file in the same namespace.
 * The externally linked object has one identity across translation units; names must be unique
 * within their namespace. Calling it uses the node's Invoke overloads and lazy registration.
 * @param NodeClass Complete class derived from TArdaDependencyNode or a domain specialization.
 * @param InvocationName Public C++ operation name, independent of the class and registry names.
 */
#define ARDA_DECLARE_GRAPH_NODE(NodeClass, InvocationName)                                                             \
	static_assert(::eastl::is_base_of_v<::arda::FArdaDependencyNodeBase, NodeClass>,                                   \
	    "Global graph operations require a dependency node class.");                                                   \
	extern const NodeClass InvocationName

/** Define the single object declared by ARDA_DECLARE_GRAPH_NODE, after including its header.
 * This creates only the callable; it does not register a definition or create device resources.
 * The first invocation registers the node on demand. NodeClass::Register() can register it earlier
 * for registry enumeration. Do not place this macro in a shared header or an anonymous namespace.
 * @param NodeClass The node class used by the declaration.
 * @param InvocationName The operation name used by the declaration.
 */
#define ARDA_DEFINE_GRAPH_NODE(NodeClass, InvocationName)                                                              \
	ARDA_DECLARE_GRAPH_NODE(NodeClass, InvocationName);                                                                \
	const NodeClass InvocationName                                                                                     \
	{                                                                                                                  \
	}
