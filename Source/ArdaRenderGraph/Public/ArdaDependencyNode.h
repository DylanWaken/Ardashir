#pragma once
#include "ArdaDependencyGraph.h"
#include <type_traits>
#include <utility>

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

	/** Default state for nodes with no device preparation or per-instance binding cache. */
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
		struct TArdaNodeRecordHook : std::false_type
		{
		};

		template <class Node>
		struct TArdaNodeRecordHook<Node,
		    std::void_t<decltype(Node::Record(std::declval<FArdaDependencyExecutionContext&>(),
		        std::declval<const typename Node::FParameters&>(),
		        std::declval<const typename Node::FState&>(),
		        std::declval<typename Node::FInstanceState&>()))>>
		    : std::is_same<FArdaRHIStatus,
		          decltype(Node::Record(std::declval<FArdaDependencyExecutionContext&>(),
		              std::declval<const typename Node::FParameters&>(),
		              std::declval<const typename Node::FState&>(),
		              std::declval<typename Node::FInstanceState&>()))>
		{
		};

		template <class Node, class = void>
		struct TArdaNodeCudaHook : std::false_type
		{
		};

		template <class Node>
		struct TArdaNodeCudaHook<Node,
		    std::void_t<decltype(Node::PrepareCuda(std::declval<FArdaDependencyExecutionContext&>(),
		        std::declval<const typename Node::FParameters&>(),
		        std::declval<const typename Node::FState&>(),
		        std::declval<typename Node::FInstanceState&>(),
		        std::declval<FArdaCudaSequence&>()))>>
		    : std::is_same<FArdaRHIStatus,
		          decltype(Node::PrepareCuda(std::declval<FArdaDependencyExecutionContext&>(),
		              std::declval<const typename Node::FParameters&>(),
		              std::declval<const typename Node::FState&>(),
		              std::declval<typename Node::FInstanceState&>(),
		              std::declval<FArdaCudaSequence&>()))>
		{
		};
	};

	/** Compile-time check for metadata, identity, description, preparation and the selected execution hook.
	 * Prepare, Validate and CreateInstance have stateless defaults in the base. Record is required for native
	 * work; PrepareCuda is required for CUDA. Synchronization nodes may omit execution entirely.
	 */
	template <class Node, class = void>
	struct TArdaDependencyNodeContract : std::false_type
	{
	};

	template <class Node>
	struct TArdaDependencyNodeContract<Node,
	    std::void_t<typename Node::FNodeBase,
	        decltype(Node::GetMetadata()),
	        decltype(Node::GetCanonicalKey(std::declval<const typename Node::FParameters&>())),
	        decltype(Node::Describe(std::declval<const typename Node::FParameters&>(),
	            std::declval<const typename Node::FState&>())),
	        decltype(Node::Prepare(std::declval<FArdaRHIDeviceRef>())),
	        decltype(Node::Validate(std::declval<const typename Node::FParameters&>())),
	        decltype(Node::CreateInstance(std::declval<FArdaRHIDeviceRef>(),
	            std::declval<const typename Node::FParameters&>(),
	            std::declval<const typename Node::FState&>()))>>
	    : std::bool_constant<std::is_base_of_v<FArdaDependencyNodeBase, Node> &&
	          std::is_same_v<typename Node::FNodeBase::FNodeType, Node> &&
	          std::is_same_v<decltype(Node::GetMetadata()), FArdaDependencyNodeMetadata> &&
	          std::is_same_v<decltype(Node::GetCanonicalKey(std::declval<const typename Node::FParameters&>())),
	              eastl::string> &&
	          std::is_same_v<decltype(Node::Describe(std::declval<const typename Node::FParameters&>(),
	                             std::declval<const typename Node::FState&>())),
	              FArdaDependencyNodeDesc> &&
	          std::is_same_v<decltype(Node::Prepare(std::declval<FArdaRHIDeviceRef>())),
	              TArdaRHIResult<eastl::shared_ptr<const typename Node::FState>>> &&
	          std::is_same_v<decltype(Node::Validate(std::declval<const typename Node::FParameters&>())),
	              FArdaRHIStatus> &&
	          std::is_same_v<decltype(Node::CreateInstance(std::declval<FArdaRHIDeviceRef>(),
	                             std::declval<const typename Node::FParameters&>(),
	                             std::declval<const typename Node::FState&>())),
	              TArdaRHIResult<eastl::shared_ptr<typename Node::FInstanceState>>> &&
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
	 * GetCanonicalKey(const FParameters&) -> eastl::string and
	 * Describe(const FParameters&, const FState&) -> FArdaDependencyNodeDesc.
	 * Native nodes implement Record(Context&, const FParameters&, const FState&, FInstanceState&)
	 * returning FArdaRHIStatus. CUDA nodes instead implement PrepareCuda with the same arguments plus
	 * FArdaCudaSequence& and append operations without submitting. Synchronization may omit execution.
	 * GetMetadata must be stable; canonical keys cover every behavior-affecting parameter and full resource
	 * identity without structure padding. Describe must declare all resource accesses and pipeline needs.
	 * Override FState/Prepare for immutable device setup and FInstanceState/CreateInstance for private
	 * mutable bindings. Opaque state types may be declared in a node header and defined in its .cpp.
	 * Preparation is fixed by node type/device; parameter-dependent setup belongs in CreateInstance and its
	 * memory requirements in Describe. Do not submit GPU work in preparation hooks.
	 * @ownership The base retains immutable parameter snapshots, prepared state and per-instance state.
	 * Nested pointees are not deep-copied; keep semantic inputs immutable and synchronize dynamic inputs.
	 * Weak device-state cache entries do not extend state or device lifetime after the last instance releases them.
	 * @errors Incomplete node contracts fail at registration/attachment compilation. Preparation failures propagate
	 * without insertion; successful null state results return InvalidState. Name collisions are InvalidArgument.
	 * @threading Registration and device preparation are serialized per class. Shared state is immutable.
	 * Graph edits and submission follow the graph's single-owner contract; distinct nodes can record concurrently.
	 * Per-instance caches may mutate during recording but must refresh when native resource identities change.
	 */
	template <class Derived, class Parameters, EArdaDependencyNodeKind Kind = EArdaDependencyNodeKind::Compute>
	class TArdaDependencyNode : public FArdaDependencyNodeBase
	{
	public:
		/** Exact CRTP base used by typed graph attachment; do not replace in the derived class. */
		using FNodeBase = TArdaDependencyNode<Derived, Parameters, Kind>;
		/** Concrete class validated by the attachment contract. */
		using FNodeType = Derived;
		/** Public attachment schema; never contains the base's private prepared-parameter wrapper. */
		using FParameters = Parameters;
		/** Default immutable state. Hide with a custom type and implement Prepare(Device) when needed. */
		using FState = FArdaEmptyDependencyNodeState;
		/** Default instance state. Hide with a custom type and implement CreateInstance when needed. */
		using FInstanceState = FArdaEmptyDependencyNodeState;
		/** Execution domain fixed by this specialization; queue selection remains ArdaInductor's responsibility. */
		static constexpr EArdaDependencyNodeKind mKind = Kind;

		/** Validate public parameters before device setup. Defaults to success. */
		static FArdaRHIStatus Validate(const FParameters&)
		{
			return {};
		}

		/** Stateless default. A derived class declaring FState must provide its own Prepare(Device). */
		template <class Node = Derived,
		    std::enable_if_t<std::is_same_v<typename Node::FState, FArdaEmptyDependencyNodeState>, int> = 0>
		static TArdaRHIResult<eastl::shared_ptr<const typename Node::FState>> Prepare(FArdaRHIDeviceRef)
		{
			static_assert(std::is_same_v<typename Node::FState, FArdaEmptyDependencyNodeState>,
			    "Nodes with custom FState must implement Prepare(Device).");
			return {eastl::make_shared<const FArdaEmptyDependencyNodeState>(), {}};
		}

		/** Stateless default. A custom FInstanceState requires CreateInstance(Device, Parameters, State). */
		template <class Node = Derived,
		    std::enable_if_t<std::is_same_v<typename Node::FInstanceState, FArdaEmptyDependencyNodeState>, int> = 0>
		static TArdaRHIResult<eastl::shared_ptr<typename Node::FInstanceState>> CreateInstance(FArdaRHIDeviceRef,
		    const FParameters&,
		    const typename Node::FState&)
		{
			static_assert(std::is_same_v<typename Node::FInstanceState, FArdaEmptyDependencyNodeState>,
			    "Nodes with custom FInstanceState must implement CreateInstance(Device, Parameters, State).");
			return {eastl::make_shared<FArdaEmptyDependencyNodeState>(), {}};
		}

		/** Registers a device-independent definition, or finds this class's matching registration.
		 * Unregistering permits later re-registration; another implementation/version under the same name fails.
		 * The explicit registry overload also supports built-in bootstrap without recursive singleton lookup.
		 */
		static FArdaRHIStatus Register(FArdaNodeRegistry& Registry)
		{
			static_assert(TArdaDependencyNodeContract<Derived>::value,
			    "Node must implement GetMetadata, GetCanonicalKey, Describe and its domain's execution hook with the base signatures.");
			static_assert(Derived::mKind == Kind, "A node's execution domain is fixed by its base specialization.");
			static std::mutex Mutex;
			std::lock_guard<std::mutex> Lock(Mutex);
			const auto Metadata = Derived::GetMetadata();
			using Bound = TBoundParameters<Derived>;
			if (const auto Existing = Registry.Find(Metadata.mName))
			{
				if (Existing->mPreparedParameterType == &ArdaDependencyParameterType<Bound> &&
				    Existing->mParameterType == &ArdaDependencyParameterType<Parameters> &&
				    Existing->mVersion == Metadata.mVersion && Existing->mKind == Kind)
				{
					return {};
				}
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Another node implementation or version owns this registry name.");
			}
			TArdaDependencyNodeDefinition<Parameters, Bound> Definition;
			Definition.mName = Metadata.mName;
			Definition.mVersion = Metadata.mVersion;
			Definition.mKind = Kind;
			Definition.mCanonicalKey = [](const Parameters& P)
			{
				return Derived::GetCanonicalKey(P);
			};
			Definition.mPrepare = PrepareParameters<Derived>;
			Definition.mDescribe = [](const Bound& P)
			{
				return Derived::Describe(*P.mParameters, *P.mPrepared->mState);
			};
			if constexpr (Kind == EArdaDependencyNodeKind::Cuda)
			{
				Definition.mPrepareCuda =
				    [](FArdaDependencyExecutionContext& C, const Bound& P, FArdaCudaSequence& Sequence)
				{
					return Derived::PrepareCuda(C, *P.mParameters, *P.mPrepared->mState, *P.mInstance, Sequence);
				};
			}
			else if constexpr (FArdaDependencyNodeBase::TArdaNodeRecordHook<Derived>::value)
			{
				Definition.mRecord = [](FArdaDependencyExecutionContext& C, const Bound& P)
				{
					return Derived::Record(C, *P.mParameters, *P.mPrepared->mState, *P.mInstance);
				};
			}
			return Registry.Register(eastl::move(Definition));
		}

		/** Register in the global node library without preparing or retaining a device. */
		static FArdaRHIStatus Register()
		{
			return Register(FArdaNodeRegistry::Get());
		}

		/** Common attachment path. The graph validates identity before preparing a new instance.
		 * Device state is weakly cached per class/device and strongly retained by live bound instances.
		 * Failures are not cached. Instance state is created separately for each new named node.
		 */
		static TArdaRHIResult<FArdaGraphNodeHandle> Attach(FArdaDependencyGraph& Graph,
		    eastl::string Name,
		    FParameters P)
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

	private:
		template <class Node>
		struct TPreparedState
		{
			FArdaRHIDeviceRef mDevice;
			eastl::shared_ptr<const typename Node::FState> mState;
		};

		template <class Node>
		struct TBoundParameters
		{
			eastl::shared_ptr<const Parameters> mParameters;
			eastl::shared_ptr<const TPreparedState<Node>> mPrepared;
			eastl::shared_ptr<typename Node::FInstanceState> mInstance;
		};

		template <class Node>
		static TArdaRHIResult<eastl::shared_ptr<const TPreparedState<Node>>> GetPreparedState(FArdaRHIDeviceRef Device)
		{
			static std::mutex Mutex;
			static eastl::vector<eastl::weak_ptr<const TPreparedState<Node>>> States;
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
			auto Owner = eastl::make_shared<TPreparedState<Node>>();
			Owner->mDevice = eastl::move(Device);
			Owner->mState = eastl::move(State.mValue);
			eastl::shared_ptr<const TPreparedState<Node>> Prepared = eastl::move(Owner);
			States.push_back(Prepared);
			return {eastl::move(Prepared), {}};
		}

		template <class Node>
		static TArdaRHIResult<eastl::shared_ptr<const TBoundParameters<Node>>> PrepareParameters(
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
			auto Bound = eastl::make_shared<TBoundParameters<Node>>();
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
