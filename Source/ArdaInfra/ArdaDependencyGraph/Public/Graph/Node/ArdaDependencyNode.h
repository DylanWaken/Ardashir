#pragma once
#include "Graph/Node/ArdaDependencyGraph.h"
#include "Graph/Node/ArdaGraphEdit.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "RHI/Scheduling/ArdaCudaSequence.h"
#include "Graph/Node/ArdaDependencyKey.h"
#include "Graph/Node/ArdaNodeParameters.h"
#include "Graph/Node/ArdaSubgraph.h"
#include <EASTL/functional.h>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>
#include <exception>

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

	/** Only metadata-backed schemas with semantic encoders may use the generated canonical key. */
	template <class Parameters, bool = TArdaNodeParameterTraits<Parameters>::value>
	struct TArdaNodeParameterKeySupport : eastl::false_type
	{
	};

	template <class Parameters>
	struct TArdaNodeParameterKeySupport<Parameters, true> : eastl::bool_constant<Parameters::SupportsNodeParameterKey()>
	{
	};

	/** Detect an optional producer-owned stage materializer without weakening the normal node contract. */
	template <class Node, class = void>
	struct TArdaNodeFetchBufferHook : eastl::false_type
	{
	};

	template <class Node>
	struct TArdaNodeFetchBufferHook<Node,
	    eastl::void_t<decltype(Node::FetchBuffer(eastl::declval<FArdaGraphEdit&>(),
	        eastl::declval<const typename Node::FArdaParameters&>(),
	        eastl::declval<const FArdaStageBufferRequest&>()))>>
	    : eastl::is_same<FArdaGraphBuffer,
	          decltype(Node::FetchBuffer(eastl::declval<FArdaGraphEdit&>(),
	              eastl::declval<const typename Node::FArdaParameters&>(),
	              eastl::declval<const FArdaStageBufferRequest&>()))>
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
		struct TArdaNodeComputeAlternativeHook : eastl::false_type
		{
		};

		template <class Node>
		struct TArdaNodeComputeAlternativeHook<Node,
		    eastl::void_t<decltype(Node::RecordComputeAlternative(eastl::declval<FArdaDependencyExecutionContext&>(),
		        eastl::declval<const typename Node::FArdaParameters&>(),
		        eastl::declval<const typename Node::FArdaState&>(),
		        eastl::declval<typename Node::FArdaInstanceState&>()))>>
		    : eastl::is_same<FArdaRHIStatus,
		          decltype(Node::RecordComputeAlternative(eastl::declval<FArdaDependencyExecutionContext&>(),
		              eastl::declval<const typename Node::FArdaParameters&>(),
		              eastl::declval<const typename Node::FArdaState&>(),
		              eastl::declval<typename Node::FArdaInstanceState&>()))>
		{
		};

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

		template <class Node, class = void>
		struct TArdaNodeNativeHook : eastl::false_type
		{
		};

		template <class Node>
		struct TArdaNodeNativeHook<Node,
		    eastl::void_t<decltype(Node::SelectNativeExecutionMode(eastl::declval<const FArdaNativeComputeContext&>(),
		                      eastl::declval<eastl::string&>())),
		        decltype(Node::PrepareNative(eastl::declval<FArdaDependencyExecutionContext&>(),
		            eastl::declval<const typename Node::FArdaParameters&>(),
		            eastl::declval<const typename Node::FArdaState&>(),
		            eastl::declval<typename Node::FArdaInstanceState&>()))>>
		    : eastl::bool_constant<eastl::is_same_v<decltype(Node::SelectNativeExecutionMode(
		                                                eastl::declval<const FArdaNativeComputeContext&>(),
		                                                eastl::declval<eastl::string&>())),
		                               TArdaRHIResult<EArdaNativeExecutionMode>> &&
		          eastl::is_same_v<decltype(Node::PrepareNative(eastl::declval<FArdaDependencyExecutionContext&>(),
		                               eastl::declval<const typename Node::FArdaParameters&>(),
		                               eastl::declval<const typename Node::FArdaState&>(),
		                               eastl::declval<typename Node::FArdaInstanceState&>())),
		              TArdaRHIResult<FArdaNativeComputePlan>>>
		{
		};
	};

	/** Compile-time check for metadata, identity, description, preparation and the selected execution hook.
	 * GetRequirements, Prepare, Validate and CreateInstance have stateless defaults in the base.
	 * Native operands provide SelectNativeExecutionMode and PrepareNative. Legacy CUDA nodes use PrepareCuda.
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
	          TArdaNodeParameterTraits<typename Node::FArdaParameters>::value &&
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
	          (FArdaDependencyNodeBase::TArdaNodeCudaHook<Node>::value ==
	              (Node::mKind == EArdaDependencyNodeKind::Cuda)) &&
	          (FArdaDependencyNodeBase::TArdaNodeNativeHook<Node>::value ==
	              (Node::mKind == EArdaDependencyNodeKind::Native)) &&
	          (!FArdaDependencyNodeBase::TArdaNodeComputeAlternativeHook<Node>::value ||
	              Node::mKind == EArdaDependencyNodeKind::Cuda) &&
	          (Node::mKind == EArdaDependencyNodeKind::Cuda || Node::mKind == EArdaDependencyNodeKind::Native
	                  ? !FArdaDependencyNodeBase::TArdaNodeRecordHook<Node>::value
	                  : FArdaDependencyNodeBase::TArdaNodeRecordHook<Node>::value ||
	                      Node::mKind == EArdaDependencyNodeKind::Synchronization)>
	{
	};

	namespace node_detail
	{
		/** Master CRTP node contract, deriving from FArdaDependencyNodeBase without virtual dispatch.
	 * Derived supplies static GetMetadata() -> FArdaDependencyNodeMetadata,
	 * GetCanonicalKey(const FArdaParameters&) -> eastl::string and
	 * Describe(const FArdaParameters&, const FArdaState&) -> FArdaDependencyNodeDesc.
	 * Macro-authored parameter structs provide default identity and resource descriptions;
	 * Describe may add pipeline contributions or parameter-dependent effects to those declarations.
	 * RHI nodes implement Record(Context&, const FArdaParameters&, const FArdaState&, FArdaInstanceState&).
	 * Native operands implement PrepareNative with the same arguments, returning a prepared facade plan.
	 * Legacy CUDA nodes use PrepareCuda with an additional FArdaCudaSequence& argument.
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
	 * Distinct threads may record private graph edit scopes concurrently; publication is atomic at Commit.
	 * Each edit belongs to one thread; submission and general graph inspection require external synchronization.
	 * Private operation state may mutate during recording, following declared resource effects and frame lifetimes.
	 */
		template <class Derived, class Parameters, EArdaDependencyNodeKind Kind = EArdaDependencyNodeKind::Compute>
		class TArdaExecutionNode : public FArdaDependencyNodeBase
		{
		public:
			/** Exact CRTP base used by typed graph attachment; do not replace in the derived class. */
			using FArdaNodeBase = TArdaExecutionNode<Derived, Parameters, Kind>;
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
			/** Ordinary nodes execute directly; the Subgraph definition specialization overrides this. */
			static constexpr bool mbSubgraph = false;
			/** Overridden only by graphics stage templates. Ordinary operations have no logical stage input. */
			static constexpr EArdaGraphicsStage mGraphicsStage = EArdaGraphicsStage::None;
			static constexpr EArdaRHIShaderStage mRayStage = EArdaRHIShaderStage::None;
			/** Override on pure graphics stages whose shaders preserve contiguous instance-range semantics. */
			static constexpr EArdaGraphicsInstancing mGraphicsInstancing = EArdaGraphicsInstancing::Disabled;

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

			/** Requirements for the optional compute alternative, independent of the CUDA route's list.
			 * Declare shared requirements in both lists. The framework never silently drops an authored requirement.
			 */
			static FArdaDependencyNodeRequirements GetComputeAlternativeRequirements(const FArdaParameters&)
			{
				return {};
			}

			static constexpr bool HasComputeAlternative()
			{
				return Kind == EArdaDependencyNodeKind::Cuda &&
				    FArdaDependencyNodeBase::TArdaNodeComputeAlternativeHook<Derived>::value;
			}

			/** Route selection is fixed before DeclareResources/Prepare. Derived nodes can qualify
			 * their CUDA variants here. Null-device analysis keeps the authored domain.
			 */
			static bool UsesComputeAlternative(const IArdaRHIDevice* Device)
			{
				return HasComputeAlternative() && Device && !Device->GetCudaCapabilities();
			}

			static void AddIntrinsicRequirements(FArdaDependencyNodeRequirements& Requirements)
			{
				AddArdaNodeIntrinsicRequirements(Requirements, Kind, Derived::mGraphicsStage, Derived::mRayStage);
			}

			/** Read-only user preflight; returns Success, Unsupported (with missing abilities), or InvalidArgument.
			 * Throwing requirement selectors or environment predicates return BackendFailure.
			 * Checks the selected route's declarations and intrinsic domain requirements without preparing resources.
			 * Descriptor-derived requirements are additionally checked by attachment and compilation.
			 */
			[[nodiscard]] static FArdaRHIStatus CheckRequiredFeatures(const IArdaRHIDevice* Device,
			    const FArdaParameters& Value)
			{
				FArdaRHIStatus Status;
				try
				{
					const bool bAlternative = HasComputeAlternative() && Derived::UsesComputeAlternative(Device);
					auto Requirements = bAlternative ? Derived::GetComputeAlternativeRequirements(Value)
					                                 : Derived::GetRequirements(Value);
					if (bAlternative)
					{
						AddArdaNodeIntrinsicRequirements(Requirements,
						    EArdaDependencyNodeKind::Compute,
						    EArdaGraphicsStage::None,
						    EArdaRHIShaderStage::None);
					}
					else
					{
						Derived::AddIntrinsicRequirements(Requirements);
					}
					Status = Requirements.Check(Device);
				}
				catch (const std::exception& Error)
				{
					Status = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
				}
				catch (...)
				{
					Status = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
					    "A required-feature selector threw an exception.");
				}
				if (!Status)
				{
					Status.mMessage = "Node '" + Derived::GetMetadata().mName + "': " + Status.mMessage;
				}
				return Status;
			}

			[[nodiscard]] static FArdaRHIStatus CheckRequiredFeatures(const FArdaRHIDeviceRef& Device,
			    const FArdaParameters& Value)
			{
				return CheckRequiredFeatures(Device.Get(), Value);
			}

			/** Declare outputs from logical inputs and scalar settings. Empty output handles are filled
		 * by the graph; explicitly supplied handles are checked against the same requirements.
		 * The default declares none. Must not submit GPU work or modify external state.
		 */
			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext&, FArdaParameters&)
			{
				return {};
			}

			/** Macro-declared fields are encoded semantically, including normalized graph resource identities.
		 * Override for schemas containing values without a built-in semantic encoder.
		 */
			template <class Node = Derived,
			    eastl::enable_if_t<TArdaNodeParameterKeySupport<typename Node::FArdaParameters>::value, int> = 0>
			static eastl::string GetCanonicalKey(const FArdaParameters& P)
			{
				return Parameters::GetNodeParameterKey(P);
			}

			/** Static resource declarations are appended by registration, including when this hook is overridden.
		 * Override to add shaders, pipeline requests or effects that depend on parameter values.
		 */
			template <class Node = Derived,
			    eastl::enable_if_t<TArdaNodeParameterTraits<typename Node::FArdaParameters>::value, int> = 0>
			static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const typename Node::FArdaState&)
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
			static TArdaRHIResult<eastl::shared_ptr<typename Node::FArdaInstanceState>> CreateInstance(
			    FArdaRHIDeviceRef,
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
				    "Node parameters must declare metadata; implement GetMetadata and the domain execution hook; GetCanonicalKey and Describe may use parameter metadata defaults. All hooks must match the base signatures.");
				static_assert(Derived::mKind == Kind, "A node's execution domain is fixed by its base specialization.");
				if constexpr (TArdaNodeParameterTraits<Parameters>::value)
				{
					const auto& ParametersMetadata = Parameters::GetNodeParameterMetadata();
					if (auto Status = ParametersMetadata.GetStatus(); !Status)
					{
						return Status;
					}
					for (const auto& Member : ParametersMetadata.GetMembers())
					{
						if (Member.mKind == EArdaNodeParameterKind::CudaResource &&
						    Kind != EArdaDependencyNodeKind::Cuda && Kind != EArdaDependencyNodeKind::Native)
						{
							return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
							    "Operand parameter mappings require a native compute execution contract.");
						}
						if (Member.mKind == EArdaNodeParameterKind::ShaderResource &&
						    Kind == EArdaDependencyNodeKind::Cuda)
						{
							return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
							    "CUDA nodes must map operand parameters instead of shader bindings.");
						}
					}
					if constexpr (Derived::mGraphicsStage == EArdaGraphicsStage::None &&
					    Derived::mRayStage == EArdaRHIShaderStage::None)
					{
						for (const auto& Member : ParametersMetadata.GetMembers())
						{
							if (Member.mKind == EArdaNodeParameterKind::StageInput ||
							    Member.mKind == EArdaNodeParameterKind::StageInputs)
							{
								return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
								    "Logical stage inputs require a graphics or ray shader stage node.");
							}
						}
					}
				}
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
				Definition.mSerializeParameters = [](const void* Value, FArdaGraphArchiveWriter& Archive)
				{
					return Parameters::GetNodeParameterMetadata().Serialize(*static_cast<const Parameters*>(Value),
					    Archive);
				};
				Definition.mDeserializeParameters =
				    [](FArdaGraphArchiveReader& Archive) -> TArdaRHIResult<eastl::shared_ptr<const void>>
				{
					auto Value = eastl::make_shared<Parameters>();
					auto Status = Parameters::GetNodeParameterMetadata().Deserialize(*Value, Archive);
					return {eastl::move(Value), eastl::move(Status)};
				};
				if constexpr (Derived::mbSubgraph)
				{
					Definition.mExpand = [](FArdaSubgraphContext& Context, const void* Value)
					{
						const auto& ParametersValue = *static_cast<const Parameters*>(Value);
						Context.BindParameters(ParametersValue);
						return Derived::Build(Context, ParametersValue);
					};
				}
				Definition.mGetRequirements = [](const void* Value)
				{
					return Derived::GetRequirements(*static_cast<const Parameters*>(Value));
				};
				Definition.mAddIntrinsicRequirements = [](FArdaDependencyNodeRequirements& Requirements)
				{
					Derived::AddIntrinsicRequirements(Requirements);
				};
				Definition.mResolveResources =
				    [](FArdaDependencyResourceContext& Context,
				        eastl::shared_ptr<const void> Value) -> TArdaRHIResult<eastl::shared_ptr<const void>>
				{
					auto Resolved =
					    eastl::make_shared<Parameters>(*eastl::static_pointer_cast<const Parameters>(Value));
					if (auto Status = Derived::DeclareResources(Context, *Resolved); !Status)
					{
						return {{}, eastl::move(Status)};
					}

					return {eastl::move(Resolved), {}};
				};
				Definition.mCanonicalKey = [](const void* Value)
				{
					const auto& P = *static_cast<const Parameters*>(Value);
					if constexpr (Derived::mGraphicsStage != EArdaGraphicsStage::None)
					{
						return FArdaDependencyKeyBuilder()
						    .String(Derived::GetCanonicalKey(P))
						    .Value(Derived::mGraphicsStage)
						    .Stage(Parameters::GetNodeParameterMetadata().GetStageInput(P))
						    .Build();
					}
					else if constexpr (Derived::mRayStage != EArdaRHIShaderStage::None)
					{
						const auto Inputs = Parameters::GetNodeParameterMetadata().GetStageInputs(P);
						FArdaDependencyKeyBuilder Key;
						Key.String(Derived::GetCanonicalKey(P))
						    .Value(Derived::mRayStage)
						    .Value(uint64_t(Inputs.size()));
						for (auto Input : Inputs)
						{
							Key.Stage(Input);
						}
						return Key.Build();
					}
					else
					{
						return Derived::GetCanonicalKey(P);
					}
				};
				Definition.mPrepareDeclaration =
				    [](FArdaRHIDeviceRef Device,
				        eastl::shared_ptr<const void> Value) -> TArdaRHIResult<eastl::shared_ptr<const void>>
				{
					auto Result = PrepareParameters<Derived>(eastl::move(Device),
					    eastl::static_pointer_cast<const Parameters>(eastl::move(Value)),
					    false);
					return {eastl::move(Result.mValue), eastl::move(Result.mStatus)};
				};
				Definition.mPrepare =
				    [](FArdaRHIDeviceRef Device,
				        eastl::shared_ptr<const void> Value) -> TArdaRHIResult<eastl::shared_ptr<const void>>
				{
					auto Result = PrepareParameters<Derived>(eastl::move(Device),
					    eastl::static_pointer_cast<const Parameters>(eastl::move(Value)));
					return {eastl::move(Result.mValue), eastl::move(Result.mStatus)};
				};
				Definition.mDescribe = [](const void* Value) -> TArdaRHIResult<FArdaDependencyNodeDesc>
				{
					const auto& P = *static_cast<const FArdaBound*>(Value);
					auto Desc = Derived::Describe(*P.mParameters, *P.mPrepared->mState);
					if (Desc.mGraphicsStage != EArdaGraphicsStage::None || Desc.mGraphicsInput ||
					    Desc.mGraphicsInput.GetKind() != EArdaGraphStageValueKind::None ||
					    Desc.mRayStage != EArdaRHIShaderStage::None || !Desc.mRayInputs.empty() ||
					    Desc.mbPipelineStageOnly)
					{
						return {{},
						    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						        "Pipeline stage identity and connections must come from TArdaNodeDefinition and node parameter metadata.")};
					}
					if constexpr (TArdaNodeParameterTraits<Parameters>::value)
					{
						const auto& Metadata = Parameters::GetNodeParameterMetadata();
						if (auto Status = Metadata.Apply(*P.mParameters, Desc); !Status)
						{
							return {{}, eastl::move(Status)};
						}
						if constexpr (Derived::mGraphicsStage != EArdaGraphicsStage::None)
						{
							if (Metadata.GetStageInputs(*P.mParameters).size() > 1)
							{
								return {{},
								    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
								        "A graphics stage must declare at most one logical stage input.")};
							}
						}
					}
					if constexpr (Derived::mGraphicsStage != EArdaGraphicsStage::None)
					{
						Desc.mGraphicsStage = Derived::mGraphicsStage;
						Desc.mGraphicsInput = Parameters::GetNodeParameterMetadata().GetStageInput(*P.mParameters);
						Desc.mbPipelineStageOnly = Derived::mGraphicsStage != EArdaGraphicsStage::Pixel;
					}
					if constexpr (Derived::mRayStage != EArdaRHIShaderStage::None)
					{
						Desc.mRayStage = Derived::mRayStage;
						Desc.mRayInputs = Parameters::GetNodeParameterMetadata().GetStageInputs(*P.mParameters);
						Desc.mbPipelineStageOnly = Derived::mRayStage != EArdaRHIShaderStage::RayGeneration;
					}
					return {eastl::move(Desc), {}};
				};
				if constexpr (Derived::mGraphicsStage != EArdaGraphicsStage::None)
				{
					Definition.mGraphicsInstancing = Derived::mGraphicsInstancing;
					Definition.mCloneGraphicsParameters =
					    [](const void* Value) -> TArdaRHIResult<eastl::shared_ptr<const void>>
					{
						const auto& P = *static_cast<const FArdaBound*>(Value);
						auto Instance =
						    Derived::CreateInstance(P.mPrepared->mDevice, *P.mParameters, *P.mPrepared->mState);
						if (!Instance)
						{
							return {{}, eastl::move(Instance.mStatus)};
						}
						if (!Instance.mValue)
						{
							return {{},
							    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
							        "CreateInstance returned no state for an assembled graphics draw.")};
						}
						auto Bound = eastl::make_shared<FArdaBound>(P);
						Bound->mInstance = eastl::move(Instance.mValue);
						return {eastl::move(Bound), {}};
					};
					Definition.mConfigureGraphics =
					    [](FArdaDependencyExecutionContext& Context, const void* Value, FArdaGraphicsWork& Work)
					{
						const auto& P = *static_cast<const FArdaBound*>(Value);
						return Derived::ConfigureGraphics(Context,
						    *P.mParameters,
						    *P.mPrepared->mState,
						    *P.mInstance,
						    Work);
					};
					if constexpr (TArdaNodeFetchBufferHook<Derived>::value)
					{
						Definition.mFetchBuffer =
						    [](FArdaGraphEdit& Edit, const void* Value, const FArdaStageBufferRequest& Request)
						{
							return Derived::FetchBuffer(Edit, *static_cast<const Parameters*>(Value), Request);
						};
					}
				}

				if constexpr (Kind == EArdaDependencyNodeKind::Native)
				{
					Definition.mSelectNativeExecutionMode =
					    [](const FArdaNativeComputeContext& Context, eastl::string& ProviderId)
					{
						return Derived::SelectNativeExecutionMode(Context, ProviderId);
					};
					Definition.mPrepareNative = [](FArdaDependencyExecutionContext& Context, const void* Value)
					{
						const auto& P = *static_cast<const FArdaBound*>(Value);
						return Derived::PrepareNative(Context, *P.mParameters, *P.mPrepared->mState, *P.mInstance);
					};
				}
				else if constexpr (Kind == EArdaDependencyNodeKind::Cuda)
				{
					Definition.mPrepareCuda =
					    [](FArdaDependencyExecutionContext& Context, const void* Value, FArdaCudaSequence& Sequence)
					{
						const auto& P = *static_cast<const FArdaBound*>(Value);
						return Derived::PrepareCuda(Context,
						    *P.mParameters,
						    *P.mPrepared->mState,
						    *P.mInstance,
						    Sequence);
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

				if constexpr (HasComputeAlternative())
				{
					auto Alternative =
					    eastl::shared_ptr<FArdaDependencyNodeExecutable>(new FArdaDependencyNodeExecutable(Definition));
					Alternative->mKind = EArdaDependencyNodeKind::Compute;
					Alternative->mPrepareCuda = {};
					Alternative->mGetRequirements = [](const void* Value)
					{
						return Derived::GetComputeAlternativeRequirements(*static_cast<const Parameters*>(Value));
					};
					Alternative->mAddIntrinsicRequirements = [](FArdaDependencyNodeRequirements& Requirements)
					{
						AddArdaNodeIntrinsicRequirements(Requirements,
						    EArdaDependencyNodeKind::Compute,
						    EArdaGraphicsStage::None,
						    EArdaRHIShaderStage::None);
					};
					Alternative->mRecord = [](FArdaDependencyExecutionContext& Context, const void* Value)
					{
						const auto& P = *static_cast<const FArdaBound*>(Value);
						return Derived::RecordComputeAlternative(Context,
						    *P.mParameters,
						    *P.mPrepared->mState,
						    *P.mInstance);
					};
					Definition.mComputeAlternative = eastl::move(Alternative);
					Definition.mSelectComputeAlternative = [](const IArdaRHIDevice* Device)
					{
						return Derived::UsesComputeAlternative(Device);
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
			template <class Initializer,
			    eastl::enable_if_t<eastl::is_invocable_v<Initializer, FArdaParameters&>, int> = 0>
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

			/** Array arguments zip by lane while scalar arguments broadcast. The same path accepts an
			 * array of complete parameter structs; results retain their per-lane resource output accessors.
			 */
			template <class... Arguments, eastl::enable_if_t<ArdaHasNodeArray<Arguments...>, int> = 0>
			static auto Invoke(FArdaGraphEdit& Edit, Arguments&&... Values)
			    -> decltype(Edit.Invoke<Derived>(eastl::forward<Arguments>(Values)...))
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
					return {{},
					    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Attach nodes inside a graph edit.")};
				}
				if (auto Status = Register(); !Status)
				{
					return {{}, eastl::move(Status)};
				}
				return Graph.AttachOrFind(eastl::move(Name), Derived::GetMetadata().mName, eastl::move(P));
			}

		private:
			template <class Node>
			struct TArdaPreparedState
			{
				FArdaRHIDeviceRef mDevice;
				eastl::shared_ptr<const typename Node::FArdaState> mState;
				bool mbComputeAlternative = false;
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
				const bool bComputeAlternative = Node::UsesComputeAlternative(Device.Get());
				for (auto It = States.begin(); It != States.end();)
				{
					if (auto Existing = It->lock())
					{
						if (Existing->mDevice == Device && Existing->mbComputeAlternative == bComputeAlternative)
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
				Owner->mbComputeAlternative = bComputeAlternative;
				eastl::shared_ptr<const TArdaPreparedState<Node>> Prepared = eastl::move(Owner);
				States.push_back(Prepared);
				return {eastl::move(Prepared), {}};
			}

			template <class Node>
			static TArdaRHIResult<eastl::shared_ptr<const TArdaBoundParameters<Node>>> PrepareParameters(
			    FArdaRHIDeviceRef Device,
			    eastl::shared_ptr<const Parameters> P,
			    bool bCreateInstance = true)
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
				auto Bound = eastl::make_shared<TArdaBoundParameters<Node>>();
				if (bCreateInstance)
				{
					auto Instance = Node::CreateInstance(Device, *P, *Prepared.mValue->mState);
					if (!Instance)
					{
						return {{}, eastl::move(Instance.mStatus)};
					}
					if (!Instance.mValue)
					{
						return {{},
						    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
						        "CreateInstance returned no instance state.")};
					}
					Bound->mInstance = eastl::move(Instance.mValue);
				}
				Bound->mParameters = eastl::move(P);
				Bound->mPrepared = eastl::move(Prepared.mValue);
				return {eastl::move(Bound), {}};
			}
		};

	}

}

/** Declare a named, immutable callable immediately after its node class at namespace scope.
 * Put the matching ARDA_DEFINE_GRAPH_NODE in exactly one source file in the same namespace.
 * The externally linked object has one identity across translation units; names must be unique
 * within their namespace. Declaration registers the type without preparing any device state.
 * @param NodeClass Complete class derived from TArdaNodeDefinition.
 * @param InvocationName Public C++ operation name, independent of the class and registry names.
 */
#define ARDA_DECLARE_GRAPH_NODE(NodeClass, InvocationName)                                                             \
	static_assert(::eastl::is_base_of_v<::arda::FArdaDependencyNodeBase, NodeClass>,                                   \
	    "Global graph operations require a dependency node class.");                                                   \
	extern const NodeClass InvocationName;                                                                             \
	inline const ::arda::FArdaRHIStatus InvocationName##RegistrationStatus = NodeClass::Register()

/** Define the single object declared by ARDA_DECLARE_GRAPH_NODE, after including its header.
 * Its matching declaration registers the definition before use, without creating device resources.
 * Do not place this macro in a shared header or an anonymous namespace.
 * @param NodeClass The node class used by the declaration.
 * @param InvocationName The operation name used by the declaration.
 */
#define ARDA_DEFINE_GRAPH_NODE(NodeClass, InvocationName) const NodeClass InvocationName = NodeClass()
