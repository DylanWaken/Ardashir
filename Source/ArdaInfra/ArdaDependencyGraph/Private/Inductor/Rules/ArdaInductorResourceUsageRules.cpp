#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Rules/ArdaInductorBindingArguments.h"

namespace arda::inductor_detail
{
	namespace
	{
		struct FArdaRequiredResourceUsage
		{
			EArdaRHIBufferUsage mBuffer = EArdaRHIBufferUsage::None;
			EArdaRHITextureUsage mTexture = EArdaRHITextureUsage::None;
		};

		FArdaRHIStatus UsageError(const FArdaDependencyResourceDesc& Resource, const eastl::string& Message)
		{
			return Error(("Resource '" + Resource.mName + "': " + Message).c_str());
		}

		void RequireState(FArdaRequiredResourceUsage& Required, EArdaRHIResourceState State, bool Texture)
		{
			if (Texture)
			{
				if (HasAnyFlags(State, EArdaRHIResourceState::ShaderResource))
				{
					Required.mTexture |= EArdaRHITextureUsage::ShaderResource;
				}
				if (HasAnyFlags(State, EArdaRHIResourceState::UnorderedAccess))
				{
					Required.mTexture |= EArdaRHITextureUsage::UnorderedAccess;
				}
				if (HasAnyFlags(State, EArdaRHIResourceState::RenderTarget))
				{
					Required.mTexture |= EArdaRHITextureUsage::RenderTarget;
				}
				if (HasAnyFlags(State, EArdaRHIResourceState::DepthRead | EArdaRHIResourceState::DepthWrite))
				{
					Required.mTexture |= EArdaRHITextureUsage::DepthStencil;
				}
				return;
			}
			if (HasAnyFlags(State, EArdaRHIResourceState::ShaderResource))
			{
				Required.mBuffer |= EArdaRHIBufferUsage::ShaderResource;
			}
			if (HasAnyFlags(State,
			        EArdaRHIResourceState::UnorderedAccess | EArdaRHIResourceState::OpacityMicromapWrite))
			{
				Required.mBuffer |= EArdaRHIBufferUsage::UnorderedAccess;
			}
			if (HasAnyFlags(State, EArdaRHIResourceState::VertexBuffer))
			{
				Required.mBuffer |= EArdaRHIBufferUsage::Vertex;
			}
			if (HasAnyFlags(State, EArdaRHIResourceState::IndexBuffer))
			{
				Required.mBuffer |= EArdaRHIBufferUsage::Index;
			}
			if (HasAnyFlags(State, EArdaRHIResourceState::ConstantBuffer))
			{
				Required.mBuffer |= EArdaRHIBufferUsage::Constant;
			}
			if (HasAnyFlags(State, EArdaRHIResourceState::IndirectArgument))
			{
				Required.mBuffer |= EArdaRHIBufferUsage::Indirect;
			}
			if (HasAnyFlags(State, EArdaRHIResourceState::AccelStructBuildInput))
			{
				Required.mBuffer |= EArdaRHIBufferUsage::AccelStructBuildInput;
			}
			if (HasAnyFlags(State,
			        EArdaRHIResourceState::AccelStructRead | EArdaRHIResourceState::AccelStructWrite |
			            EArdaRHIResourceState::AccelStructBuildBlas))
			{
				Required.mBuffer |= EArdaRHIBufferUsage::AccelStructStorage;
			}
			if (HasAnyFlags(State, EArdaRHIResourceState::OpacityMicromapBuildInput))
			{
				Required.mBuffer |= EArdaRHIBufferUsage::OpacityMicromapBuildInput;
			}
		}

		FArdaRHIStatus CollectResourceUsage(const FArdaDependencyGraph::FArdaImpl& Graph,
		    eastl::vector<FArdaRequiredResourceUsage>& Required)
		{
			Required.resize(Graph.mResources.size());
			for (size_t I = 0; I < Graph.mResources.size(); ++I)
			{
				const auto& Resource = Graph.mResources[I];
				if (!Resource.mExternalAccelerationStructure)
				{
					RequireState(Required[I],
					    Resource.mbTexture ? Resource.mTexture.mInitialState : Resource.mBuffer.mInitialState,
					    Resource.mbTexture);
				}
			}

			const auto RequireBinding = [&](EArdaRHIBindingType Type,
			                                FArdaDependencyResourceHandle Handle,
			                                const FArdaRHIViewDesc& View) -> FArdaRHIStatus
			{
				if (Type == EArdaRHIBindingType::Sampler)
				{
					return {};
				}
				if (!Graph.HasResource(Handle))
				{
					return Error("Shader binding references a foreign or missing resource during usage deduction.");
				}
				const auto& Resource = Graph.mResources[Handle.mIndex];
				auto& Usage = Required[Handle.mIndex];
				if (Type == EArdaRHIBindingType::RayTracingAccelStruct)
				{
					return Resource.mExternalAccelerationStructure
					    ? FArdaRHIStatus{}
					    : UsageError(Resource,
					          "Acceleration structure binding requires an imported acceleration structure.");
				}
				const bool Texture = Type == EArdaRHIBindingType::TextureSRV || Type == EArdaRHIBindingType::TextureUAV;
				if (Resource.mbTexture != Texture || Resource.mExternalAccelerationStructure)
				{
					return UsageError(Resource, "Shader binding type does not match the resource kind.");
				}
				if (Texture)
				{
					Usage.mTexture |= Type == EArdaRHIBindingType::TextureUAV ? EArdaRHITextureUsage::UnorderedAccess
					                                                          : EArdaRHITextureUsage::ShaderResource;
					if (View.mFormat != EArdaRHIFormat::Unknown && View.mFormat != Resource.mTexture.mFormat)
					{
						auto Compatible = Resource.mTexture;
						Compatible.mUsage |= EArdaRHITextureUsage::Typeless;
						if (!IsArdaRHITextureViewFormatCompatible(Compatible, View.mFormat))
						{
							return UsageError(Resource, "Shader view format is incompatible with the texture format.");
						}
						Usage.mTexture |= EArdaRHITextureUsage::Typeless;
					}
					return {};
				}
				switch (Type)
				{
				case EArdaRHIBindingType::ConstantBuffer:
					Usage.mBuffer |= EArdaRHIBufferUsage::Constant;
					return {};
				case EArdaRHIBindingType::VolatileConstantBuffer:
					Usage.mBuffer |= EArdaRHIBufferUsage::Constant | EArdaRHIBufferUsage::Volatile;
					return {};
				case EArdaRHIBindingType::StructuredBufferSRV:
				case EArdaRHIBindingType::StructuredBufferUAV:
					Usage.mBuffer |= EArdaRHIBufferUsage::Structured;
					break;
				case EArdaRHIBindingType::RawBufferSRV:
				case EArdaRHIBindingType::RawBufferUAV:
					Usage.mBuffer |= EArdaRHIBufferUsage::Raw;
					break;
				case EArdaRHIBindingType::TypedBufferSRV:
				case EArdaRHIBindingType::TypedBufferUAV:
				{
					const auto Format =
					    View.mFormat == EArdaRHIFormat::Unknown ? Resource.mBuffer.mFormat : View.mFormat;
					if (!IsArdaRHIFormatKnown(Format) || GetArdaRHIFormatInfo(Format).mbDepth ||
					    !GetArdaRHIFormatElementSize(Format))
					{
						return UsageError(Resource,
						    "Typed buffer bindings require an uncompressed color view or buffer format.");
					}
					break;
				}
				default:
					return UsageError(Resource, "Unsupported resource binding type during usage deduction.");
				}
				const bool UAV = Type == EArdaRHIBindingType::StructuredBufferUAV ||
				    Type == EArdaRHIBindingType::RawBufferUAV || Type == EArdaRHIBindingType::TypedBufferUAV;
				Usage.mBuffer |= UAV ? EArdaRHIBufferUsage::UnorderedAccess : EArdaRHIBufferUsage::ShaderResource;
				return {};
			};
			const auto RequireSchema = [&](const FArdaDependencyShaderBindings& Bindings) -> FArdaRHIStatus
			{
				if (!Bindings.mMetadata || !Bindings.mMetadata->GetStatus())
				{
					return Error("Usage deduction requires valid shader parameter metadata.");
				}
				eastl::vector<FArdaFlattenedShaderParameterMember> Members;
				Bindings.mMetadata->GetFlattenedMembers(Members);
				for (const auto& Member : Members)
				{
					const auto& Desc = *Member.mMember;
					if (Desc.mKind == EArdaShaderParameterKind::Value ||
					    Desc.mKind == EArdaShaderParameterKind::PushConstants)
					{
						continue;
					}
					for (uint32_t I = 0; I < Desc.mArrayCount; ++I)
					{
						const auto* Argument = FindArgument(Bindings, Member.mPath, I);
						if (!Argument)
						{
							return Error(
							    ("Missing shader resource argument during usage deduction: " + Member.mPath).c_str());
						}
						if (auto Status = RequireBinding(Desc.mBindingType, Argument->mResource, Argument->mView);
						    !Status)
						{
							return Status;
						}
					}
				}
				return {};
			};
			for (auto Handle : Graph.mTopology.GetNodes())
			{
				const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
				for (const auto& Output : Node.mOutputs)
				{
					if (!Graph.HasResource(Output.mResource))
					{
						return Error("Node output references a foreign or missing resource during usage deduction.");
					}
					Required[Output.mResource.mIndex].mBuffer |= Output.mRequiredBufferUsage;
					Required[Output.mResource.mIndex].mTexture |= Output.mRequiredTextureUsage;
				}
				for (const auto& Access : Node.mDesc.mAccesses)
				{
					if (!Graph.HasResource(Access.mResource))
					{
						return Error("Node access references a foreign or missing resource during usage deduction.");
					}
					const auto& Resource = Graph.mResources[Access.mResource.mIndex];
					if (!Resource.mExternalAccelerationStructure)
					{
						RequireState(Required[Access.mResource.mIndex], Access.mState, Resource.mbTexture);
					}
				}
				for (auto Target : Node.mDesc.mColorTargets)
				{
					if (!Graph.HasResource(Target) || !Graph.mResources[Target.mIndex].mbTexture)
					{
						return Error("Color attachment references a foreign, missing or non-texture resource.");
					}
					Required[Target.mIndex].mTexture |= EArdaRHITextureUsage::RenderTarget;
				}
				if (const auto Target = Node.mDesc.mDepthTarget; Target)
				{
					if (!Graph.HasResource(Target) || !Graph.mResources[Target.mIndex].mbTexture)
					{
						return Error("Depth attachment references a foreign, missing or non-texture resource.");
					}
					Required[Target.mIndex].mTexture |= EArdaRHITextureUsage::DepthStencil;
				}
				for (const auto& Bindings : Node.mDesc.mShaderBindings)
				{
					if (auto Status = RequireSchema(Bindings); !Status)
					{
						return Status;
					}
				}
				for (const auto& Table : Node.mDesc.mBindlessTables)
				{
					for (const auto& Entry : Table.mEntries)
					{
						if (auto Status = RequireBinding(Entry.mType, Entry.mResource, Entry.mView); !Status)
						{
							return Status;
						}
					}
				}
				for (const auto& Table : Node.mDesc.mShaderTables)
				{
					for (const auto& Record : Table.mRecords)
					{
						if (Record.mLocalBindings.mMetadata)
						{
							if (auto Status = RequireSchema(Record.mLocalBindings); !Status)
							{
								return Status;
							}
						}
					}
				}
			}
			return {};
		}

		template <class Usage>
		FArdaRHIStatus ResolveUsage(Usage& Value, Usage Required, const Usage* External, bool Deduce)
		{
			const bool Automatic = HasAnyFlags(Value, Usage::AutoDeduction);
			if (Automatic && !Deduce)
			{
				return Error("AutoDeduction is unresolved; run FArdaDeduceResourceUsageRule before compilation.");
			}
			Value = static_cast<Usage>(static_cast<uint16_t>(Value) & ~static_cast<uint16_t>(Usage::AutoDeduction));
			if (Automatic)
			{
				Value |= Required;
			}
			if ((Value & Required) != Required)
			{
				return Error(
				    "Explicit usage is missing capabilities required by a node; add the flags or use AutoDeduction.");
			}
			if (External)
			{
				if (HasAnyFlags(*External, Usage::AutoDeduction) || (*External & Value) != Value)
				{
					return Error("Imported native resource usage cannot satisfy the graph's required capabilities.");
				}
				if (Automatic)
				{
					Value = *External;
				}
			}
			return {};
		}

		FArdaRHIStatus CheckResourceUsage(const FArdaDependencyGraph::FArdaImpl& Graph,
		    eastl::vector<FArdaDependencyResourceDesc>* Resolved)
		{
			eastl::vector<FArdaRequiredResourceUsage> Required;
			if (auto Status = CollectResourceUsage(Graph, Required); !Status)
			{
				return Status;
			}
			if (Resolved)
			{
				*Resolved = Graph.mResources;
			}
			for (size_t I = 0; I < Graph.mResources.size(); ++I)
			{
				auto Resource = Graph.mResources[I];
				if (Resource.mbTexture && !Resource.mExternalAccelerationStructure)
				{
					const auto* External =
					    Resource.mExternalTexture ? &Resource.mExternalTexture->GetDesc().mUsage : nullptr;
					if (auto Status =
					        ResolveUsage(Resource.mTexture.mUsage, Required[I].mTexture, External, Resolved != nullptr);
					    !Status)
					{
						return UsageError(Resource, Status.mMessage);
					}
					if (auto Status = Validate(Resource.mTexture); !Status)
					{
						return UsageError(Resource, Status.mMessage);
					}
				}
				else if (!Resource.mExternalAccelerationStructure)
				{
					const auto* External =
					    Resource.mExternalBuffer ? &Resource.mExternalBuffer->GetDesc().mUsage : nullptr;
					if (auto Status =
					        ResolveUsage(Resource.mBuffer.mUsage, Required[I].mBuffer, External, Resolved != nullptr);
					    !Status)
					{
						return UsageError(Resource, Status.mMessage);
					}
					if (auto Status = Validate(Resource.mBuffer); !Status)
					{
						return UsageError(Resource, Status.mMessage);
					}
				}
				// Inactive descriptor slots must not retain the default sentinel either.
				if (Resource.mbTexture || Resource.mExternalAccelerationStructure)
				{
					if (auto Status = ResolveUsage(Resource.mBuffer.mUsage,
					        EArdaRHIBufferUsage::None,
					        static_cast<const EArdaRHIBufferUsage*>(nullptr),
					        Resolved != nullptr);
					    !Status)
					{
						return UsageError(Resource, Status.mMessage);
					}
				}
				if (!Resource.mbTexture || Resource.mExternalAccelerationStructure)
				{
					if (auto Status = ResolveUsage(Resource.mTexture.mUsage,
					        EArdaRHITextureUsage::None,
					        static_cast<const EArdaRHITextureUsage*>(nullptr),
					        Resolved != nullptr);
					    !Status)
					{
						return UsageError(Resource, Status.mMessage);
					}
				}
				if (Resolved)
				{
					(*Resolved)[I] = eastl::move(Resource);
				}
			}
			// Reuse native descriptor validation for state/resource-kind and CPU heap restrictions.
			const auto& Resources = Resolved ? *Resolved : Graph.mResources;
			for (auto Handle : Graph.mTopology.GetNodes())
			{
				const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
				for (const auto& Access : Node.mDesc.mAccesses)
				{
					const auto& Resource = Resources[Access.mResource.mIndex];
					if (Resource.mExternalAccelerationStructure)
					{
						continue;
					}
					FArdaRHIStatus Status;
					if (Resource.mbTexture)
					{
						auto Desc = Resource.mTexture;
						Desc.mInitialState = Access.mState;
						Status = Validate(Desc);
					}
					else
					{
						auto Desc = Resource.mBuffer;
						Desc.mInitialState = Access.mState;
						Status = Validate(Desc);
					}
					if (!Status)
					{
						return UsageError(Resource, "Node '" + Node.mName + "': " + Status.mMessage);
					}
				}
			}
			return {};
		}
	}

	FArdaRHIStatus ValidateResourceUsage(const FArdaDependencyGraph::FArdaImpl& Graph)
	{
		return CheckResourceUsage(Graph, nullptr);
	}
}

namespace arda
{
	FArdaRHIStatus FArdaDeduceResourceUsageRule::Apply(FArdaInductorGraph& Graph) const
	{
		auto& State = FArdaInductorGraphAccess::State(Graph);
		eastl::vector<FArdaDependencyResourceDesc> Resolved;
		if (auto Status = inductor_detail::CheckResourceUsage(State, &Resolved); !Status)
		{
			return Status;
		}
		State.mResources = eastl::move(Resolved);
		return {};
	}
}
