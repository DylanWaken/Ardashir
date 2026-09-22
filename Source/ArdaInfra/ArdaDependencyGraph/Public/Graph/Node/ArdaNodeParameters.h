/** Aggregate node parameters with immutable dependency metadata and semantic identity. */
#pragma once

#include "Graph/Node/ArdaDependencyKey.h"
#include "Graph/Node/ArdaDependencyNodeDesc.h"
#include "Graph/Resources/ArdaGraphValue.h"
#include "RHI/Shaders/ArdaComputeParameters.h"
#include <EASTL/array.h>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>

namespace arda
{
	/** Extend for application value types by supplying mSupported=true and Append(Key, Value).
	 * Encode semantic fields individually; never encode padding or native pointer addresses.
	 * An unsupported field requires the owning node to provide GetCanonicalKey instead.
	 */
	template <class T, class = void>
	struct TArdaNodeParameterKey
	{
		static constexpr bool mSupported = false;
	};

	template <class T>
	struct TArdaNodeParameterKey<T, eastl::enable_if_t<eastl::is_arithmetic_v<T> || eastl::is_enum_v<T>>>
	{
		static constexpr bool mSupported = sizeof(T) <= sizeof(uint64_t);

		static void Append(FArdaDependencyKeyBuilder& Key, T Value)
		{
			Key.Value(Value);
		}
	};

	template <>
	struct TArdaNodeParameterKey<FArdaDependencyResourceHandle>
	{
		static constexpr bool mSupported = true;

		static void Append(FArdaDependencyKeyBuilder& Key, FArdaDependencyResourceHandle Value)
		{
			Key.Resource(Value);
		}
	};

	template <EArdaGraphResourceKind Kind>
	struct TArdaNodeParameterKey<TArdaGraphValue<Kind>> : TArdaNodeParameterKey<FArdaDependencyResourceHandle>
	{
	};

	template <>
	struct TArdaNodeParameterKey<FArdaGraphStageValue>
	{
		static constexpr bool mSupported = true;

		static void Append(FArdaDependencyKeyBuilder& Key, FArdaGraphStageValue Value)
		{
			Key.Stage(Value);
		}
	};

	template <EArdaGraphStageValueKind Kind>
	struct TArdaNodeParameterKey<TArdaGraphStageValue<Kind>> : TArdaNodeParameterKey<FArdaGraphStageValue>
	{
	};

	template <>
	struct TArdaNodeParameterKey<eastl::string>
	{
		static constexpr bool mSupported = true;

		static void Append(FArdaDependencyKeyBuilder& Key, const eastl::string& Value)
		{
			Key.String(Value);
		}
	};

	template <>
	struct TArdaNodeParameterKey<FArdaRHIBufferRange>
	{
		static constexpr bool mSupported = true;

		static void Append(FArdaDependencyKeyBuilder& Key, const FArdaRHIBufferRange& Value)
		{
			Key.BufferRange(Value);
		}
	};

	template <>
	struct TArdaNodeParameterKey<FArdaRHITextureSubresourceRange>
	{
		static constexpr bool mSupported = true;

		static void Append(FArdaDependencyKeyBuilder& Key, const FArdaRHITextureSubresourceRange& Value)
		{
			Key.TextureRange(Value);
		}
	};

	template <>
	struct TArdaNodeParameterKey<FArdaRHIViewDesc>
	{
		static constexpr bool mSupported = true;

		static void Append(FArdaDependencyKeyBuilder& Key, const FArdaRHIViewDesc& Value)
		{
			Key.Value(Value.mFormat)
			    .Value(Value.mDimension)
			    .BufferRange(Value.mBufferRange)
			    .TextureRange(Value.mTextureRange);
		}
	};

	template <class T, size_t Count>
	struct TArdaNodeParameterKey<eastl::array<T, Count>>
	{
		static constexpr bool mSupported = TArdaNodeParameterKey<T>::mSupported;

		static void Append(FArdaDependencyKeyBuilder& Key, const eastl::array<T, Count>& Values)
		{
			Key.Value(static_cast<uint64_t>(Count));
			for (const auto& Value : Values)
			{
				TArdaNodeParameterKey<T>::Append(Key, Value);
			}
		}
	};

	template <class T, class Allocator>
	struct TArdaNodeParameterKey<eastl::vector<T, Allocator>>
	{
		static constexpr bool mSupported = TArdaNodeParameterKey<T>::mSupported;

		static void Append(FArdaDependencyKeyBuilder& Key, const eastl::vector<T, Allocator>& Values)
		{
			Key.Value(static_cast<uint64_t>(Values.size()));
			for (const auto& Value : Values)
			{
				TArdaNodeParameterKey<T>::Append(Key, Value);
			}
		}
	};

	/** Dependency role of a declared node parameter. */
	enum class EArdaNodeParameterKind : uint8_t
	{
		Value,
		StageInput,
		StageInputs,
		Buffer,
		Texture,
		AccelerationStructure,
		ColorTarget,
		DepthTarget,
		ShaderResource,
		CudaResource
	};

	/** Inspectable declaration data, independent of any parameter instance or structure layout. */
	struct FArdaNodeParameterMember
	{
		const char* mName = nullptr;
		EArdaNodeParameterKind mKind = EArdaNodeParameterKind::Value;
		EArdaDependencyAccess mAccess = EArdaDependencyAccess::Read;
		EArdaRHIResourceState mState = EArdaRHIResourceState::Unknown;
		const FArdaShaderParameterMetadata* mShaderMetadata = nullptr;
		const char* mShaderMember = nullptr;
		const char* mPipelineSlot = "default";
		uint32_t mArrayElement = 0;
		FArdaRHIViewDesc mView;
		/** C++ graph field category and element count, independently checked against the source schema. */
		EArdaGraphResourceKind mResourceKind = EArdaGraphResourceKind::Buffer;
		uint32_t mElementCount = 1;
		const FArdaComputeParameterMetadata* mComputeMetadata = nullptr;
		const char* mComputeMember = nullptr;
	};

	/** Validate mappings against the referenced executable parameter schema before resource preparation. */
	FArdaRHIStatus ValidateArdaNodeShaderParameter(const FArdaNodeParameterMember& Member);
	FArdaRHIStatus ValidateArdaNodeCudaParameter(const FArdaNodeParameterMember& Member);
	EArdaDependencyAccess GetArdaNodeShaderParameterAccess(const FArdaNodeParameterMember& Member);
	EArdaDependencyAccess GetArdaNodeCudaParameterAccess(const FArdaNodeParameterMember& Member);
	class FArdaDependencyExecutionContext;
	FArdaRHIStatus ResolveArdaNodeCudaResource(FArdaDependencyExecutionContext& Context,
	    const FArdaNodeParameterMember& Member,
	    FArdaDependencyResourceHandle Resource,
	    void* HostParameters);

	/** Exact typed field identities; application wrappers must not masquerade as a resource mapping. */
	template <class T>
	struct TArdaNodeResourceField : eastl::false_type
	{
	};

	template <EArdaGraphResourceKind Kind>
	struct TArdaNodeResourceField<TArdaGraphValue<Kind>> : eastl::true_type
	{
		static constexpr EArdaGraphResourceKind mKind = Kind;
		static constexpr uint32_t mCount = 1;
	};

	template <EArdaGraphResourceKind Kind, size_t Count>
	struct TArdaNodeResourceField<eastl::array<TArdaGraphValue<Kind>, Count>> : eastl::true_type
	{
		static_assert(Count && Count <= UINT32_MAX, "Resource mapping arrays require a nonzero 32-bit count.");
		static constexpr EArdaGraphResourceKind mKind = Kind;
		static constexpr uint32_t mCount = uint32_t(Count);
	};

	/** CUDA schemas expose metadata in every domain; only their retained host storage may be resolved. */
	template <class T, class = void>
	struct TArdaNodeCudaHostStorage : eastl::true_type
	{
	};

	template <class T>
	struct TArdaNodeCudaHostStorage<T, eastl::void_t<typename T::FArdaDomain, typename T::FArdaParameters>>
	    : eastl::is_same<T, typename T::FArdaParameters>
	{
	};

	/** Typed callbacks retain pointer-to-member access rather than byte offsets into Parameters. */
	template <class Parameters>
	struct TArdaNodeParameterMember : FArdaNodeParameterMember
	{
		void (*mApply)(const Parameters&, const FArdaNodeParameterMember&, FArdaDependencyNodeDesc&) = nullptr;
		FArdaDependencyResourceHandle (*mGetResource)(const Parameters&) = nullptr;
		void (*mAppendStages)(const Parameters&, eastl::vector<FArdaGraphStageValue>&) = nullptr;
		void (*mAppendKey)(const Parameters&, FArdaDependencyKeyBuilder&) = nullptr;
	};

	/** Finds or creates the shared binding declaration for one shader schema and pipeline slot. */
	inline FArdaDependencyShaderBindings& FindArdaNodeShaderBindings(FArdaDependencyNodeDesc& Description,
	    const FArdaNodeParameterMember& Member)
	{
		for (auto& Bindings : Description.mShaderBindings)
		{
			if (Bindings.mMetadata == Member.mShaderMetadata &&
			    (Bindings.mPipelineSlot.empty() ? eastl::string("default") : Bindings.mPipelineSlot) ==
			        Member.mPipelineSlot)
			{
				return Bindings;
			}
		}
		Description.mShaderBindings.push_back({Member.mShaderMetadata, Member.mPipelineSlot, {}, {}});
		return Description.mShaderBindings.back();
	}

	/** Appends one resource requirement; shader hazards are derived by existing shader metadata handling. */
	inline void AppendArdaNodeParameterResource(FArdaDependencyResourceHandle Resource,
	    const FArdaNodeParameterMember& Member,
	    FArdaDependencyNodeDesc& Description,
	    uint32_t ArrayOffset = 0)
	{
		if (Member.mKind == EArdaNodeParameterKind::ShaderResource)
		{
			FindArdaNodeShaderBindings(Description, Member)
			    .mResources.push_back({Member.mShaderMember,
			        Resource,
			        Member.mArrayElement + ArrayOffset,
			        GetArdaNodeShaderParameterAccess(Member),
			        Member.mView,
			        {}});
			return;
		}
		if (Member.mKind == EArdaNodeParameterKind::CudaResource)
		{
			Description.mAccesses.push_back({Resource,
			    GetArdaNodeCudaParameterAccess(Member),
			    EArdaRHIResourceState::UnorderedAccess,
			    Member.mView.mBufferRange,
			    Member.mView.mTextureRange});
			return;
		}
		if (Member.mKind == EArdaNodeParameterKind::ColorTarget)
		{
			Description.mColorTargets.push_back(Resource);
		}
		else if (Member.mKind == EArdaNodeParameterKind::DepthTarget)
		{
			Description.mDepthTarget = Resource;
		}
		Description.mAccesses.push_back(
		    {Resource, Member.mAccess, Member.mState, Member.mView.mBufferRange, Member.mView.mTextureRange});
	}

	/** Recognizes supported array/vector fields without requiring arbitrary values to be iterable. */
	template <class T>
	struct TArdaNodeParameterSequence : eastl::false_type
	{
	};

	template <class T, size_t Count>
	struct TArdaNodeParameterSequence<eastl::array<T, Count>> : eastl::true_type
	{
		using FArdaElement = T;
	};

	template <class T, class Allocator>
	struct TArdaNodeParameterSequence<eastl::vector<T, Allocator>> : eastl::true_type
	{
		using FArdaElement = T;
	};

	/** Constructs metadata using an actual C++ member pointer, including for non-standard-layout fields. */
	template <EArdaNodeParameterKind Kind, auto Field, class Parameters>
	TArdaNodeParameterMember<Parameters> MakeArdaNodeParameter(FArdaNodeParameterMember Declaration)
	{
		using FArdaValue = eastl::remove_cv_t<eastl::remove_reference_t<decltype(eastl::declval<Parameters>().*Field)>>;
		TArdaNodeParameterMember<Parameters> Member;
		static_cast<FArdaNodeParameterMember&>(Member) = eastl::move(Declaration);
		Member.mKind = Kind;
		if constexpr (Kind == EArdaNodeParameterKind::ShaderResource || Kind == EArdaNodeParameterKind::CudaResource)
		{
			static_assert(TArdaNodeResourceField<FArdaValue>::value, "Schema mappings require typed graph resources.");
			Member.mResourceKind = TArdaNodeResourceField<FArdaValue>::mKind;
			Member.mElementCount = TArdaNodeResourceField<FArdaValue>::mCount;
		}
		if constexpr (eastl::is_convertible_v<FArdaValue, FArdaDependencyResourceHandle>)
		{
			Member.mGetResource = [](const Parameters& Values) -> FArdaDependencyResourceHandle
			{
				return Values.*Field;
			};
		}
		if constexpr (Kind == EArdaNodeParameterKind::StageInput)
		{
			static_assert(eastl::is_base_of_v<FArdaGraphStageValue, FArdaValue>,
			    "Stage inputs require a graph stage value.");
			Member.mAppendStages = [](const Parameters& Values, eastl::vector<FArdaGraphStageValue>& Stages)
			{
				Stages.push_back(Values.*Field);
			};
		}
		else if constexpr (Kind == EArdaNodeParameterKind::StageInputs)
		{
			static_assert(TArdaNodeParameterSequence<FArdaValue>::value,
			    "Stage input lists require an array or vector.");
			static_assert(eastl::is_base_of_v<FArdaGraphStageValue,
			                  typename TArdaNodeParameterSequence<FArdaValue>::FArdaElement>,
			    "Stage input lists require graph stage values.");
			Member.mAppendStages = [](const Parameters& Values, eastl::vector<FArdaGraphStageValue>& Stages)
			{
				for (const auto& Stage : Values.*Field)
				{
					Stages.push_back(Stage);
				}
			};
		}
		else if constexpr (Kind != EArdaNodeParameterKind::Value)
		{
			Member.mApply = [](const Parameters& Values,
			                    const FArdaNodeParameterMember& Definition,
			                    FArdaDependencyNodeDesc& Description)
			{
				if constexpr (TArdaNodeParameterSequence<FArdaValue>::value)
				{
					uint32_t Index = 0;
					for (const auto& Resource : Values.*Field)
					{
						AppendArdaNodeParameterResource(Resource, Definition, Description, Index++);
					}
				}
				else
				{
					AppendArdaNodeParameterResource(Values.*Field, Definition, Description);
				}
			};
		}
		if constexpr (TArdaNodeParameterKey<FArdaValue>::mSupported)
		{
			Member.mAppendKey = [](const Parameters& Values, FArdaDependencyKeyBuilder& Key)
			{
				TArdaNodeParameterKey<FArdaValue>::Append(Key, Values.*Field);
			};
		}
		return Member;
	}

	/** Immutable static metadata shared by every instance of a macro-authored aggregate. */
	template <class Parameters>
	class TArdaNodeParameterMetadata final
	{
	public:
		TArdaNodeParameterMetadata(const char* Name, eastl::vector<TArdaNodeParameterMember<Parameters>> Members)
		    : mName(Name),
		      mMembers(eastl::move(Members))
		{
		}

		const char* GetName() const noexcept
		{
			return mName;
		}

		const eastl::vector<TArdaNodeParameterMember<Parameters>>& GetMembers() const noexcept
		{
			return mMembers;
		}

		/** Rejects malformed schema declarations before preparing node/device state. */
		FArdaRHIStatus GetStatus() const
		{
			uint32_t DepthCount = 0;
			for (const auto& Member : mMembers)
			{
				if (Member.mKind == EArdaNodeParameterKind::DepthTarget && ++DepthCount > 1)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "Node parameter metadata may declare only one depth target.");
				}
				if (Member.mKind == EArdaNodeParameterKind::ShaderResource)
				{
					if (auto Status = ValidateArdaNodeShaderParameter(Member); !Status)
					{
						return Status;
					}
				}
				else if (Member.mKind == EArdaNodeParameterKind::CudaResource)
				{
					if (auto Status = ValidateArdaNodeCudaParameter(Member); !Status)
					{
						return Status;
					}
				}
				for (const auto& Other : mMembers)
				{
					if (&Other == &Member)
					{
						break;
					}
					const bool SameShader = Member.mKind == EArdaNodeParameterKind::ShaderResource &&
					    Other.mKind == Member.mKind && Member.mShaderMetadata == Other.mShaderMetadata &&
					    std::strcmp(Member.mPipelineSlot, Other.mPipelineSlot) == 0 &&
					    std::strcmp(Member.mShaderMember, Other.mShaderMember) == 0;
					const bool SameCuda = Member.mKind == EArdaNodeParameterKind::CudaResource &&
					    Other.mKind == Member.mKind && Member.mComputeMetadata == Other.mComputeMetadata &&
					    std::strcmp(Member.mComputeMember, Other.mComputeMember) == 0;
					if ((SameShader || SameCuda) &&
					    uint64_t(Member.mArrayElement) < uint64_t(Other.mArrayElement) + Other.mElementCount &&
					    uint64_t(Other.mArrayElement) < uint64_t(Member.mArrayElement) + Member.mElementCount)
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						    "Node parameter mappings overlap the same executable resource member/array element.");
					}
				}
			}
			return {};
		}

		/** Appends declared dependencies; errors leave the supplied description unchanged. */
		FArdaRHIStatus Apply(const Parameters& Values, FArdaDependencyNodeDesc& Description) const
		{
			if (auto Status = GetStatus(); !Status)
			{
				return Status;
			}
			for (const auto& Member : mMembers)
			{
				if (Member.mKind == EArdaNodeParameterKind::ShaderResource)
				{
					for (const auto& Bindings : Description.mShaderBindings)
					{
						const auto Slot = Bindings.mPipelineSlot.empty() ? "default" : Bindings.mPipelineSlot.c_str();
						if (Bindings.mMetadata != Member.mShaderMetadata ||
						    std::strcmp(Slot, Member.mPipelineSlot) != 0)
						{
							continue;
						}
						for (const auto& Existing : Bindings.mResources)
						{
							if (Existing.mMember == Member.mShaderMember &&
							    Existing.mArrayElement >= Member.mArrayElement &&
							    uint64_t(Existing.mArrayElement) <
							        uint64_t(Member.mArrayElement) + Member.mElementCount)
							{
								return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
								    "Node Describe and parameter metadata duplicate a shader resource mapping.");
							}
						}
					}
				}
				if (Member.mKind == EArdaNodeParameterKind::DepthTarget && Description.mDepthTarget &&
				    Description.mDepthTarget != Member.mGetResource(Values))
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "Node Describe and parameter metadata declare conflicting depth targets.");
				}
			}
			for (const auto& Member : mMembers)
			{
				if (Member.mApply)
				{
					Member.mApply(Values, Member, Description);
				}
			}
			Description.mbDeclarativeParameters = true;
			return {};
		}

		/** Resolves the same mapped fields used for dependency declarations into their operand schema.
		 * Scalar host values are preserved; every resource member must have exactly one mapping.
		 * The destination remains unchanged on failure. No CUDA work is submitted.
		 */
		template <class HostParameters>
		FArdaRHIStatus ResolveCudaParameters(const Parameters& Values,
		    FArdaDependencyExecutionContext& Context,
		    HostParameters& Host) const
		{
			if (auto Status = GetStatus(); !Status)
			{
				return Status;
			}
			const auto& Schema = HostParameters::GetStaticMetadata();
			if (auto Status = Schema.GetStatus(); !Status)
			{
				return Status;
			}
			if (!TArdaNodeCudaHostStorage<HostParameters>::value || !eastl::is_standard_layout_v<HostParameters> ||
			    Schema.GetSize() != sizeof(HostParameters) || Schema.GetAlignment() != alignof(HostParameters))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "CUDA parameter resolution requires matching retained host storage, not device or rebound arguments.");
			}
			HostParameters Resolved = Host;
			for (const auto& Required : Schema.GetMembers())
			{
				if (Required.mKind == EArdaComputeParameterKind::Value)
				{
					continue;
				}
				const TArdaNodeParameterMember<Parameters>* Mapping = nullptr;
				for (const auto& Member : mMembers)
				{
					if (Member.mKind == EArdaNodeParameterKind::CudaResource && Member.mComputeMetadata == &Schema &&
					    std::strcmp(Member.mComputeMember, Required.mName) == 0)
					{
						Mapping = &Member;
						break;
					}
				}
				if (!Mapping)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "CUDA operand resources require complete node parameter mappings from the same schema.");
				}
				if (auto Status =
				        ResolveArdaNodeCudaResource(Context, *Mapping, Mapping->mGetResource(Values), &Resolved);
				    !Status)
				{
					return Status;
				}
			}
			Host = eastl::move(Resolved);
			return {};
		}

		eastl::vector<FArdaGraphStageValue> GetStageInputs(const Parameters& Values) const
		{
			eastl::vector<FArdaGraphStageValue> Stages;
			for (const auto& Member : mMembers)
			{
				if (Member.mAppendStages)
				{
					Member.mAppendStages(Values, Stages);
				}
			}
			return Stages;
		}

		/** Graphics stages have one predecessor; callers validate additional stage inputs separately. */
		FArdaGraphStageValue GetStageInput(const Parameters& Values) const
		{
			const auto Stages = GetStageInputs(Values);
			return Stages.empty() ? FArdaGraphStageValue{} : Stages.front();
		}

		/** Requires all fields to have semantic encoders; unsupported fields never hash object bytes. */
		eastl::string GetCanonicalKey(const Parameters& Values) const
		{
			static_assert(Parameters::SupportsNodeParameterKey(),
			    "Provide Node::GetCanonicalKey or specialize TArdaNodeParameterKey for every unsupported field type.");
			FArdaDependencyKeyBuilder Key;
			Key.String(mName);
			for (const auto& Member : mMembers)
			{
				Key.String(Member.mName);
				Member.mAppendKey(Values, Key);
			}
			return Key.Build();
		}

	private:
		const char* mName;
		eastl::vector<TArdaNodeParameterMember<Parameters>> mMembers;
	};

	/** Detects declarative parameter structs without imposing requirements on existing plain structs. */
	template <class Parameters, class = void>
	struct TArdaNodeParameterTraits : eastl::false_type
	{
	};

	template <class Parameters>
	struct TArdaNodeParameterTraits<Parameters, eastl::void_t<decltype(Parameters::GetNodeParameterMetadata())>>
	    : eastl::true_type
	{
	};

	/** Fixed shader resource arrays used by declaration macros without comma-sensitive type expansion. */
	template <size_t Count>
	using TArdaNodeBufferArray = eastl::array<FArdaGraphBuffer, Count>;
	template <size_t Count>
	using TArdaNodeTextureArray = eastl::array<FArdaGraphTexture, Count>;

	/** Resolves CUDA resource fields using the node's canonical operand-member mappings. */
	template <class Parameters, class HostParameters>
	FArdaRHIStatus ResolveNodeCudaParameters(FArdaDependencyExecutionContext& Context,
	    const Parameters& Values,
	    HostParameters& Host)
	{
		return Parameters::GetNodeParameterMetadata().ResolveCudaParameters(Values, Context, Host);
	}
}

/** Internal declaration chain. Every instance field stays public and aggregate-initializable. */
#define ARDA_INTERNAL_NODE_PARAMETER(CppType, MemberName, DefaultValue, Kind, ...)                                     \
	FArdaNodeMemberId##MemberName;                                                                                     \
                                                                                                                       \
public:                                                                                                                \
	CppType MemberName = DefaultValue;                                                                                 \
                                                                                                                       \
private:                                                                                                               \
	struct FArdaNodeNextMemberId##MemberName                                                                           \
	{                                                                                                                  \
	};                                                                                                                 \
	static void FArdaAppendNodeMembers(FArdaNodeNextMemberId##MemberName,                                              \
	    eastl::vector<::arda::TArdaNodeParameterMember<FArdaNodeThisStruct>>& Members)                                 \
	{                                                                                                                  \
		FArdaAppendNodeMembers(FArdaNodeMemberId##MemberName{}, Members);                                              \
		Members.push_back(::arda::MakeArdaNodeParameter<Kind, &FArdaNodeThisStruct::MemberName, FArdaNodeThisStruct>(  \
		    {#MemberName, Kind, __VA_ARGS__}));                                                                        \
	}                                                                                                                  \
	static constexpr bool FArdaSupportsNodeKey(FArdaNodeNextMemberId##MemberName)                                      \
	{                                                                                                                  \
		return FArdaSupportsNodeKey(FArdaNodeMemberId##MemberName{}) &&                                                \
		    ::arda::TArdaNodeParameterKey<CppType>::mSupported;                                                        \
	}                                                                                                                  \
	typedef FArdaNodeNextMemberId##MemberName

/** Begins a constructor-free node parameter aggregate; do not add undeclared instance fields. */
#define ARDA_BEGIN_NODE_PARAMETER_STRUCT(StructType)                                                                   \
	struct StructType                                                                                                  \
	{                                                                                                                  \
	private:                                                                                                           \
		using FArdaNodeThisStruct = StructType;                                                                        \
		static constexpr const char* mArdaNodeStructName = #StructType;                                                \
		struct FArdaNodeFirstMemberId                                                                                  \
		{                                                                                                              \
		};                                                                                                             \
		static void FArdaAppendNodeMembers(FArdaNodeFirstMemberId,                                                     \
		    eastl::vector<::arda::TArdaNodeParameterMember<FArdaNodeThisStruct>>&)                                     \
		{                                                                                                              \
		}                                                                                                              \
		static constexpr bool FArdaSupportsNodeKey(FArdaNodeFirstMemberId)                                             \
		{                                                                                                              \
			return true;                                                                                               \
		}                                                                                                              \
		typedef FArdaNodeFirstMemberId

/** Ends the aggregate and exposes its immutable metadata and automatic semantic key. */
#define ARDA_END_NODE_PARAMETER_STRUCT()                                                                               \
	FArdaNodeLastMemberId;                                                                                             \
                                                                                                                       \
public:                                                                                                                \
	static const ::arda::TArdaNodeParameterMetadata<FArdaNodeThisStruct>& GetNodeParameterMetadata()                   \
	{                                                                                                                  \
		static const ::arda::TArdaNodeParameterMetadata<FArdaNodeThisStruct> Metadata = []                             \
		{                                                                                                              \
			eastl::vector<::arda::TArdaNodeParameterMember<FArdaNodeThisStruct>> Members;                              \
			FArdaAppendNodeMembers(FArdaNodeLastMemberId{}, Members);                                                  \
			return ::arda::TArdaNodeParameterMetadata<FArdaNodeThisStruct>(mArdaNodeStructName, eastl::move(Members)); \
		}();                                                                                                           \
		return Metadata;                                                                                               \
	}                                                                                                                  \
	static constexpr bool SupportsNodeParameterKey()                                                                   \
	{                                                                                                                  \
		return FArdaSupportsNodeKey(FArdaNodeLastMemberId{});                                                          \
	}                                                                                                                  \
	template <class FArdaParameters = FArdaNodeThisStruct>                                                             \
	static eastl::string GetNodeParameterKey(const FArdaParameters& Parameters)                                        \
	{                                                                                                                  \
		return GetNodeParameterMetadata().GetCanonicalKey(Parameters);                                                 \
	}                                                                                                                  \
	}                                                                                                                  \
	;

/** Declares an ordinary value without adding a dependency. */
#define ARDA_NODE_PARAMETER(CppType, MemberName)                                                                       \
	ARDA_INTERNAL_NODE_PARAMETER(CppType,                                                                              \
	    MemberName,                                                                                                    \
	    {},                                                                                                            \
	    ::arda::EArdaNodeParameterKind::Value,                                                                         \
	    ::arda::EArdaDependencyAccess::Read)

/** Declares a value with an explicit default, preserving positional aggregate initialization. */
#define ARDA_NODE_PARAMETER_WITH_DEFAULT(CppType, MemberName, ...)                                                     \
	ARDA_INTERNAL_NODE_PARAMETER(CppType,                                                                              \
	    MemberName,                                                                                                    \
	    (__VA_ARGS__),                                                                                                 \
	    ::arda::EArdaNodeParameterKind::Value,                                                                         \
	    ::arda::EArdaDependencyAccess::Read)

/** Declares a typed logical graphics/ray predecessor, independent of the C++ member name. */
#define ARDA_NODE_STAGE_INPUT(CppType, MemberName)                                                                     \
	ARDA_INTERNAL_NODE_PARAMETER(CppType,                                                                              \
	    MemberName,                                                                                                    \
	    {},                                                                                                            \
	    ::arda::EArdaNodeParameterKind::StageInput,                                                                    \
	    ::arda::EArdaDependencyAccess::Read)

/** Declares an ordered vector of logical ray-shader predecessors. */
#define ARDA_NODE_STAGE_INPUTS(CppType, MemberName)                                                                    \
	ARDA_INTERNAL_NODE_PARAMETER(eastl::vector<CppType>,                                                               \
	    MemberName,                                                                                                    \
	    {},                                                                                                            \
	    ::arda::EArdaNodeParameterKind::StageInputs,                                                                   \
	    ::arda::EArdaDependencyAccess::Read)

/** Declares a buffer and its non-shader access/state requirement. */
#define ARDA_NODE_BUFFER(MemberName, Access, State)                                                                    \
	ARDA_INTERNAL_NODE_PARAMETER(::arda::FArdaGraphBuffer,                                                             \
	    MemberName,                                                                                                    \
	    {},                                                                                                            \
	    ::arda::EArdaNodeParameterKind::Buffer,                                                                        \
	    Access,                                                                                                        \
	    State)

/** Declares a texture and its non-shader access/state requirement. */
#define ARDA_NODE_TEXTURE(MemberName, Access, State)                                                                   \
	ARDA_INTERNAL_NODE_PARAMETER(::arda::FArdaGraphTexture,                                                            \
	    MemberName,                                                                                                    \
	    {},                                                                                                            \
	    ::arda::EArdaNodeParameterKind::Texture,                                                                       \
	    Access,                                                                                                        \
	    State)

/** Declares an acceleration structure and its non-shader access/state requirement. */
#define ARDA_NODE_ACCELERATION_STRUCTURE(MemberName, Access, State)                                                    \
	ARDA_INTERNAL_NODE_PARAMETER(::arda::FArdaGraphAccelerationStructure,                                              \
	    MemberName,                                                                                                    \
	    {},                                                                                                            \
	    ::arda::EArdaNodeParameterKind::AccelerationStructure,                                                         \
	    Access,                                                                                                        \
	    State)

/** Declares a color output. Declaration order is the shader's render-target slot order. */
#define ARDA_NODE_COLOR_TARGET(MemberName)                                                                             \
	ARDA_NODE_COLOR_TARGET_ACCESS(MemberName, ::arda::EArdaDependencyAccess::Write)

/** Declares a color output that consumes previous contents, for example blending or attachment loads. */
#define ARDA_NODE_COLOR_TARGET_ACCESS(MemberName, Access)                                                              \
	ARDA_INTERNAL_NODE_PARAMETER(::arda::FArdaGraphTexture,                                                            \
	    MemberName,                                                                                                    \
	    {},                                                                                                            \
	    ::arda::EArdaNodeParameterKind::ColorTarget,                                                                   \
	    Access,                                                                                                        \
	    ::arda::EArdaRHIResourceState::RenderTarget)

/** Declares depth access; Read selects DepthRead and Write/ReadWrite select DepthWrite. */
#define ARDA_NODE_DEPTH_TARGET(MemberName, Access)                                                                     \
	ARDA_INTERNAL_NODE_PARAMETER(::arda::FArdaGraphTexture,                                                            \
	    MemberName,                                                                                                    \
	    {},                                                                                                            \
	    ::arda::EArdaNodeParameterKind::DepthTarget,                                                                   \
	    Access,                                                                                                        \
	    (Access) == ::arda::EArdaDependencyAccess::Read ? ::arda::EArdaRHIResourceState::DepthRead                     \
	                                                    : ::arda::EArdaRHIResourceState::DepthWrite)

/** Full shader mapping; View and Slot are immutable declaration expressions. ShaderMember is a member token/path. */
#define ARDA_INTERNAL_NODE_SHADER_RESOURCE(CppType,                                                                    \
    MemberName,                                                                                                        \
    ShaderParameters,                                                                                                  \
    ShaderMember,                                                                                                      \
    Slot,                                                                                                              \
    Access,                                                                                                            \
    Element,                                                                                                           \
    View)                                                                                                              \
	ARDA_INTERNAL_NODE_PARAMETER(CppType,                                                                              \
	    MemberName,                                                                                                    \
	    {},                                                                                                            \
	    ::arda::EArdaNodeParameterKind::ShaderResource,                                                                \
	    Access,                                                                                                        \
	    ::arda::EArdaRHIResourceState::Unknown,                                                                        \
	    &ShaderParameters::GetStaticMetadata(),                                                                        \
	    #ShaderMember,                                                                                                 \
	    Slot,                                                                                                          \
	    Element,                                                                                                       \
	    View)

/** Maps a buffer to a shader member, inheriting its canonical access requirement. */
#define ARDA_NODE_SHADER_BUFFER(MemberName, ShaderParameters, ShaderMember)                                            \
	ARDA_NODE_SHADER_BUFFER_ACCESS(MemberName,                                                                         \
	    ShaderParameters,                                                                                              \
	    ShaderMember,                                                                                                  \
	    ::arda::EArdaDependencyAccess::AutoDeduction)

/** Maps a texture to a shader member, inheriting its canonical access requirement. */
#define ARDA_NODE_SHADER_TEXTURE(MemberName, ShaderParameters, ShaderMember)                                           \
	ARDA_NODE_SHADER_TEXTURE_ACCESS(MemberName,                                                                        \
	    ShaderParameters,                                                                                              \
	    ShaderMember,                                                                                                  \
	    ::arda::EArdaDependencyAccess::AutoDeduction)

/** Overrides the shader buffer's UAV access without duplicating binding kinds or resource states. */
#define ARDA_NODE_SHADER_BUFFER_ACCESS(MemberName, ShaderParameters, ShaderMember, Access)                             \
	ARDA_NODE_SHADER_BUFFER_BINDING(MemberName,                                                                        \
	    ShaderParameters,                                                                                              \
	    ShaderMember,                                                                                                  \
	    "default",                                                                                                     \
	    Access,                                                                                                        \
	    0,                                                                                                             \
	    ::arda::FArdaRHIViewDesc{})

/** Overrides the shader texture's UAV access without duplicating binding kinds or resource states. */
#define ARDA_NODE_SHADER_TEXTURE_ACCESS(MemberName, ShaderParameters, ShaderMember, Access)                            \
	ARDA_NODE_SHADER_TEXTURE_BINDING(MemberName,                                                                       \
	    ShaderParameters,                                                                                              \
	    ShaderMember,                                                                                                  \
	    "default",                                                                                                     \
	    Access,                                                                                                        \
	    0,                                                                                                             \
	    ::arda::FArdaRHIViewDesc{})

/** Maps one buffer with an explicit pipeline slot, shader array element and immutable resource view. */
#define ARDA_NODE_SHADER_BUFFER_BINDING(MemberName, ShaderParameters, ShaderMember, Slot, Access, Element, View)       \
	ARDA_INTERNAL_NODE_SHADER_RESOURCE(::arda::FArdaGraphBuffer,                                                       \
	    MemberName,                                                                                                    \
	    ShaderParameters,                                                                                              \
	    ShaderMember,                                                                                                  \
	    Slot,                                                                                                          \
	    Access,                                                                                                        \
	    Element,                                                                                                       \
	    View)

/** Maps one texture with an explicit pipeline slot, shader array element and immutable resource view. */
#define ARDA_NODE_SHADER_TEXTURE_BINDING(MemberName, ShaderParameters, ShaderMember, Slot, Access, Element, View)      \
	ARDA_INTERNAL_NODE_SHADER_RESOURCE(::arda::FArdaGraphTexture,                                                      \
	    MemberName,                                                                                                    \
	    ShaderParameters,                                                                                              \
	    ShaderMember,                                                                                                  \
	    Slot,                                                                                                          \
	    Access,                                                                                                        \
	    Element,                                                                                                       \
	    View)

/** Maps an imported acceleration structure to its declared shader resource. */
#define ARDA_NODE_SHADER_ACCELERATION_STRUCTURE(MemberName, ShaderParameters, ShaderMember)                            \
	ARDA_INTERNAL_NODE_SHADER_RESOURCE(::arda::FArdaGraphAccelerationStructure,                                        \
	    MemberName,                                                                                                    \
	    ShaderParameters,                                                                                              \
	    ShaderMember,                                                                                                  \
	    "default",                                                                                                     \
	    ::arda::EArdaDependencyAccess::Read,                                                                           \
	    0,                                                                                                             \
	    ::arda::FArdaRHIViewDesc{})

/** Declares a fixed shader buffer array. Access applies to UAV members; SRVs remain read-only. */
#define ARDA_NODE_SHADER_BUFFER_ARRAY(MemberName, ShaderParameters, ShaderMember, Count, Access)                       \
	ARDA_INTERNAL_NODE_SHADER_RESOURCE(ARDA_INTERNAL_NODE_BUFFER_ARRAY_TYPE(Count),                                    \
	    MemberName,                                                                                                    \
	    ShaderParameters,                                                                                              \
	    ShaderMember,                                                                                                  \
	    "default",                                                                                                     \
	    Access,                                                                                                        \
	    0,                                                                                                             \
	    ::arda::FArdaRHIViewDesc{})

/** Declares a fixed shader texture array. Access applies to UAV members; SRVs remain read-only. */
#define ARDA_NODE_SHADER_TEXTURE_ARRAY(MemberName, ShaderParameters, ShaderMember, Count, Access)                      \
	ARDA_INTERNAL_NODE_SHADER_RESOURCE(ARDA_INTERNAL_NODE_TEXTURE_ARRAY_TYPE(Count),                                   \
	    MemberName,                                                                                                    \
	    ShaderParameters,                                                                                              \
	    ShaderMember,                                                                                                  \
	    "default",                                                                                                     \
	    Access,                                                                                                        \
	    0,                                                                                                             \
	    ::arda::FArdaRHIViewDesc{})

/** Internal aliases keep array type commas inside a parenthesized macro expansion. */
#define ARDA_INTERNAL_NODE_BUFFER_ARRAY_TYPE(Count) ::arda::TArdaNodeBufferArray<Count>
#define ARDA_INTERNAL_NODE_TEXTURE_ARRAY_TYPE(Count) ::arda::TArdaNodeTextureArray<Count>

/** Internal CUDA mapping; source operand metadata owns resource type and access direction. */
#define ARDA_INTERNAL_NODE_CUDA_RESOURCE(CppType, MemberName, CudaParameters, CudaMember, View)                        \
	ARDA_INTERNAL_NODE_PARAMETER(CppType,                                                                              \
	    MemberName,                                                                                                    \
	    {},                                                                                                            \
	    ::arda::EArdaNodeParameterKind::CudaResource,                                                                  \
	    ::arda::EArdaDependencyAccess::AutoDeduction,                                                                  \
	    ::arda::EArdaRHIResourceState::UnorderedAccess,                                                                \
	    nullptr,                                                                                                       \
	    nullptr,                                                                                                       \
	    "default",                                                                                                     \
	    0,                                                                                                             \
	    View,                                                                                                          \
	    ::arda::EArdaGraphResourceKind::Buffer,                                                                        \
	    1,                                                                                                             \
	    &CudaParameters::GetStaticMetadata(),                                                                          \
	    #CudaMember)

/** Binds a logical buffer to its CUDA operand member and inherits the schema's access direction. */
#define ARDA_NODE_CUDA_BUFFER(MemberName, CudaParameters, CudaMember)                                                  \
	ARDA_INTERNAL_NODE_CUDA_RESOURCE(::arda::FArdaGraphBuffer,                                                         \
	    MemberName,                                                                                                    \
	    CudaParameters,                                                                                                \
	    CudaMember,                                                                                                    \
	    ::arda::FArdaRHIViewDesc{})

/** Binds a logical texture to its CUDA operand member and inherits the schema's access direction. */
#define ARDA_NODE_CUDA_TEXTURE(MemberName, CudaParameters, CudaMember)                                                 \
	ARDA_INTERNAL_NODE_CUDA_RESOURCE(::arda::FArdaGraphTexture,                                                        \
	    MemberName,                                                                                                    \
	    CudaParameters,                                                                                                \
	    CudaMember,                                                                                                    \
	    ::arda::FArdaRHIViewDesc{})
