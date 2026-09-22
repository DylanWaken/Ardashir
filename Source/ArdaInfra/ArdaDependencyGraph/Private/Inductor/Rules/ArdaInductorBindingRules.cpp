#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Rules/ArdaInductorBindingArguments.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
		FArdaInductorPipelineRequest* FindRequest(FArdaDependencyNodeDesc& D, eastl::string& Slot)
		{
			if (Slot.empty())
			{
				Slot = "default";
			}
			for (auto& P : D.mPipelines)
			{
				if ((P.mSlot.empty() ? eastl::string("default") : P.mSlot) == Slot)
				{
					return &P;
				}
			}
			return nullptr;
		}

		TArdaRHIResult<FArdaRHIBindingLayoutRef> InternLayout(FArdaDependencyGraph::FArdaImpl& G,
		    const FArdaRHIBindingLayoutDesc& D)
		{
			for (const auto& L : G.mAutomaticLayouts)
			{
				if (!L->GetBindlessDesc() && L->GetDesc() == D)
				{
					return {L, {}};
				}
			}
			auto R = G.mDevice->CreateBindingLayout(D);
			if (R)
			{
				G.mAutomaticLayouts.push_back(R.mValue);
			}
			return R;
		}

		TArdaRHIResult<FArdaRHIBindingLayoutRef> InternLayout(FArdaDependencyGraph::FArdaImpl& G,
		    const FArdaRHIBindlessLayoutDesc& D)
		{
			for (const auto& L : G.mAutomaticLayouts)
			{
				if (L->GetBindlessDesc() && *L->GetBindlessDesc() == D)
				{
					return {L, {}};
				}
			}
			auto R = G.mDevice->CreateBindlessLayout(D);
			if (R)
			{
				G.mAutomaticLayouts.push_back(R.mValue);
			}
			return R;
		}

		FArdaRHIStatus DescribeResource(const FArdaDependencyGraph::FArdaImpl& G,
		    FArdaDependencyNodeDesc& D,
		    EArdaRHIBindingType Type,
		    FArdaDependencyResourceHandle Resource,
		    const FArdaRHIViewDesc& View,
		    EArdaDependencyAccess Access,
		    const FArdaRHISamplerRef& Sampler)
		{
			if (Type == EArdaRHIBindingType::Sampler)
			{
				return Sampler && !Resource ? FArdaRHIStatus{}
				                            : BindingError("A sampler argument requires only a retained sampler.");
			}
			if (Sampler || !G.HasResource(Resource))
			{
				return BindingError("Shader argument has a foreign, stale or missing resource.");
			}
			const auto& R = G.mResources[Resource.mIndex];
			bool Texture = false, Accel = false, UAV = false, Constant = false;
			switch (Type)
			{
			case EArdaRHIBindingType::TextureSRV:
				Texture = true;
				break;
			case EArdaRHIBindingType::TextureUAV:
				Texture = UAV = true;
				break;
			case EArdaRHIBindingType::TypedBufferSRV:
			case EArdaRHIBindingType::StructuredBufferSRV:
			case EArdaRHIBindingType::RawBufferSRV:
				break;
			case EArdaRHIBindingType::TypedBufferUAV:
			case EArdaRHIBindingType::StructuredBufferUAV:
			case EArdaRHIBindingType::RawBufferUAV:
				UAV = true;
				break;
			case EArdaRHIBindingType::ConstantBuffer:
			case EArdaRHIBindingType::VolatileConstantBuffer:
				Constant = true;
				break;
			case EArdaRHIBindingType::RayTracingAccelStruct:
				Accel = true;
				break;
			default:
				return BindingError("Unsupported automatic shader resource binding type.");
			}
			if (Texture != R.mbTexture || Accel != bool(R.mExternalAccelerationStructure))
			{
				return BindingError("Shader argument resource kind mismatch.");
			}
			if (Access > EArdaDependencyAccess::ReadWrite)
			{
				return BindingError("Invalid shader access direction.");
			}
			FArdaDependencyAccess A;
			A.mResource = Resource;
			A.mAccess = UAV ? Access : EArdaDependencyAccess::Read;
			A.mState = UAV ? EArdaRHIResourceState::UnorderedAccess
			    : Accel    ? EArdaRHIResourceState::AccelStructRead
			    : Constant ? EArdaRHIResourceState::ConstantBuffer
			               : EArdaRHIResourceState::ShaderResource;
			A.mBufferRange = View.mBufferRange;
			A.mTextureRange = View.mTextureRange;
			D.mAccesses.push_back(A);
			return {};
		}

		FArdaRHIStatus DescribeSchema(const FArdaDependencyGraph::FArdaImpl& G,
		    FArdaDependencyNodeDesc& D,
		    const FArdaDependencyShaderBindings& B,
		    bool Local = false)
		{
			if (!B.mMetadata || !B.mMetadata->GetStatus())
			{
				return BindingError("Invalid shader argument metadata.");
			}
			eastl::vector<FArdaFlattenedShaderParameterMember> Members;
			B.mMetadata->GetFlattenedMembers(Members);
			size_t Required = 0, Values = 0;
			for (const auto& F : Members)
			{
				const auto& M = *F.mMember;
				if (M.mKind == EArdaShaderParameterKind::Value)
				{
					continue;
				}
				if (M.mKind == EArdaShaderParameterKind::PushConstants)
				{
					if (Local)
					{
						return BindingError("Ray local argument bytes belong in mLocalArguments.");
					}
					++Values;
					const auto V = eastl::find_if(B.mValues.begin(),
					    B.mValues.end(),
					    [&](const auto& X)
					    {
						    return X.mMember == F.mPath;
					    });
					if (V == B.mValues.end() || V->mBytes.size() != M.mSize)
					{
						return BindingError("Missing or incorrectly sized push-constant argument: " + F.mPath);
					}
					continue;
				}
				for (uint32_t E = 0; E < M.mArrayCount; ++E)
				{
					++Required;
					const auto* R = FindArgument(B, F.mPath, E);
					if (!R)
					{
						return BindingError("Missing shader argument: " + F.mPath);
					}
					const auto RequiredAccess = M.GetResourceAccess();
					const auto DeclaredAccess = RequiredAccess == EArdaShaderResourceAccess::Read
					    ? EArdaDependencyAccess::Read
					    : RequiredAccess == EArdaShaderResourceAccess::Write ? EArdaDependencyAccess::Write
					                                                         : EArdaDependencyAccess::ReadWrite;
					const auto Access =
					    R->mAccess == EArdaDependencyAccess::AutoDeduction ? DeclaredAccess : R->mAccess;
					const bool UAV = M.mKind == EArdaShaderParameterKind::BufferUAV ||
					    M.mKind == EArdaShaderParameterKind::TextureUAV;
					if (Access > EArdaDependencyAccess::ReadWrite || (!UAV && Access != EArdaDependencyAccess::Read) ||
					    (Access != DeclaredAccess && Access != EArdaDependencyAccess::ReadWrite))
					{
						return BindingError("Node access does not cover its shader parameter requirement: " + F.mPath);
					}
					if (auto S = DescribeResource(G, D, M.mBindingType, R->mResource, R->mView, Access, R->mSampler);
					    !S)
					{
						return S;
					}
				}
			}
			if (Required != B.mResources.size() || Values != B.mValues.size())
			{
				return BindingError("Shader arguments contain duplicate or unknown members/elements.");
			}
			return {};
		}

	}

	FArdaRHIStatus DescribeArdaShaderAccesses(const FArdaDependencyGraph::FArdaImpl& G, FArdaDependencyNodeDesc& D)
	{
		for (const auto& B : D.mShaderBindings)
		{
			if (auto S = DescribeSchema(G, D, B); !S)
			{
				return S;
			}
		}
		for (const auto& Table : D.mBindlessTables)
		{
			for (const auto& R : Table.mEntries)
			{
				if (auto S = DescribeResource(G, D, R.mType, R.mResource, R.mView, R.mAccess, R.mSampler); !S)
				{
					return S;
				}
			}
		}
		for (const auto& Table : D.mShaderTables)
		{
			for (const auto& R : Table.mRecords)
			{
				if (R.mLocalBindings.mMetadata)
				{
					if (auto S = DescribeSchema(G, D, R.mLocalBindings, true); !S)
					{
						return S;
					}
				}
			}
		}
		return {};
	}

	FArdaRHIStatus DescribeArdaShaderBindings(FArdaDependencyGraph::FArdaImpl& G, FArdaDependencyNodeDesc& D)
	{
		if (D.mbPipelineStageOnly &&
		    (!D.mShaderBindings.empty() || !D.mBindlessTables.empty() || !D.mShaderTables.empty()))
		{
			return BindingError(
			    "Stage declarations cannot bind physical resources; bind them on the executable consumer.");
		}

		// Derive fixed layouts and resource effects from the shared shader-parameter metadata.
		for (auto& B : D.mShaderBindings)
		{
			auto* P = FindRequest(D, B.mPipelineSlot);
			if (!P)
			{
				return BindingError("Shader arguments name an undeclared pipeline slot.");
			}
			if (auto S = DescribeSchema(G, D, B); !S)
			{
				return S;
			}
			if (P->mKind == EArdaPipelineStateKind::WorkGraph && !B.mValues.empty())
			{
				return BindingError(
				    "Work-graph scalar parameters must use entry records or declared constant buffers.");
			}
			if (!G.mDevice)
			{
				return BindingError("Automatic shader layout preparation requires a device.");
			}
			eastl::vector<FArdaRHIBindingLayoutDesc> Generated;
			if (auto S = B.mMetadata->BuildBindingLayoutDescs(Generated); !S)
			{
				return BindingError(S.mMessage);
			}
			for (const auto& Layout : Generated)
			{
				auto Created = InternLayout(G, Layout);
				if (!Created)
				{
					return Created.mStatus;
				}
				P->mBindingLayouts.push_back(eastl::move(Created.mValue));
			}
		}

		// Validate table identities and indexed views before interning their native layout semantics.
		for (size_t I = 0; I < D.mBindlessTables.size(); ++I)
		{
			auto& B = D.mBindlessTables[I];
			auto* P = FindRequest(D, B.mPipelineSlot);
			if (!P || B.mName.empty() || B.mEntries.empty() || !B.mLayout.mMaxCapacity)
			{
				return BindingError(
				    "A bindless table needs a name, pipeline slot, entries and bounded maximum capacity.");
			}
			for (size_t J = 0; J < I; ++J)
			{
				if (D.mBindlessTables[J].mName == B.mName ||
				    (D.mBindlessTables[J].mPipelineSlot == B.mPipelineSlot &&
				        D.mBindlessTables[J].mLayout.mRegisterSpace == B.mLayout.mRegisterSpace))
				{
					return BindingError("Duplicate bindless table name or register space.");
				}
			}
			uint32_t Required = 0;
			for (size_t E = 0; E < B.mEntries.size(); ++E)
			{
				const auto& R = B.mEntries[E];
				if (R.mArrayElement >= B.mLayout.mMaxCapacity)
				{
					return BindingError("Bindless index exceeds maximum capacity.");
				}
				Required = eastl::max(Required, R.mArrayElement + 1);
				if (!eastl::any_of(B.mLayout.mRegisterSpaces.begin(),
				        B.mLayout.mRegisterSpaces.end(),
				        [&](const auto& L)
				        {
					        return uint64_t(L.mSlot) + B.mLayout.mFirstSlot == R.mSlot && L.mType == R.mType;
				        }))
				{
					return BindingError("Bindless entry does not match a declared register/type.");
				}
				for (size_t J = 0; J < E; ++J)
				{
					if (B.mEntries[J].mSlot == R.mSlot && B.mEntries[J].mType == R.mType &&
					    B.mEntries[J].mArrayElement == R.mArrayElement)
					{
						return BindingError("Duplicate bindless entry index.");
					}
				}
				const bool ReadOnly = R.mType == EArdaRHIBindingType::TextureSRV ||
				    R.mType == EArdaRHIBindingType::TypedBufferSRV ||
				    R.mType == EArdaRHIBindingType::StructuredBufferSRV ||
				    R.mType == EArdaRHIBindingType::RawBufferSRV || R.mType == EArdaRHIBindingType::ConstantBuffer ||
				    R.mType == EArdaRHIBindingType::VolatileConstantBuffer ||
				    R.mType == EArdaRHIBindingType::RayTracingAccelStruct || R.mType == EArdaRHIBindingType::Sampler;
				if (ReadOnly && R.mAccess != EArdaDependencyAccess::Read)
				{
					return BindingError("A read-only bindless descriptor cannot declare a write.");
				}
				if (auto S = DescribeResource(G, D, R.mType, R.mResource, R.mView, R.mAccess, R.mSampler); !S)
				{
					return S;
				}
			}
			if (!B.mCapacity)
			{
				B.mCapacity = Required;
			}
			if (B.mCapacity < Required || B.mCapacity > B.mLayout.mMaxCapacity)
			{
				return BindingError("Invalid bindless table capacity.");
			}
			if (!G.mDevice)
			{
				return BindingError("Bindless layout preparation requires a device.");
			}
			auto Created = InternLayout(G, B.mLayout);
			if (!Created)
			{
				return Created.mStatus;
			}
			P->mBindingLayouts.push_back(eastl::move(Created.mValue));
		}

		// Ray records add local layouts and geometry dependencies to the same node description.
		for (size_t I = 0; I < D.mShaderTables.size(); ++I)
		{
			auto& T = D.mShaderTables[I];
			auto* P = FindRequest(D, T.mPipelineSlot);
			if (!P || P->mKind != EArdaPipelineStateKind::RayTracing)
			{
				return BindingError("Shader table requires a ray-tracing pipeline slot.");
			}
			for (size_t J = 0; J < I; ++J)
			{
				if (D.mShaderTables[J].mPipelineSlot == T.mPipelineSlot)
				{
					return BindingError("Duplicate shader table slot.");
				}
			}
			for (auto& R : T.mRecords)
			{
				if (R.mLocalBindings.mMetadata)
				{
					if (auto S = DescribeSchema(G, D, R.mLocalBindings, true); !S)
					{
						return S;
					}
					if (!G.mDevice)
					{
						return BindingError("Ray local parameter preparation requires a device.");
					}
					eastl::vector<FArdaRHIBindingLayoutDesc> Layouts;
					if (auto S = R.mLocalBindings.mMetadata->BuildBindingLayoutDescs(Layouts); !S)
					{
						return BindingError(S.mMessage);
					}
					if (Layouts.size() != 1)
					{
						return BindingError("A ray record requires exactly one local parameter layout.");
					}
					auto Layout = InternLayout(G, Layouts.front());
					if (!Layout)
					{
						return Layout.mStatus;
					}
					P->mLocalBindingLayouts.push_back({R.mExportName, eastl::move(Layout.mValue)});
				}
				if (R.mGeometry)
				{
					if (auto S = DescribeResource(G,
					        D,
					        EArdaRHIBindingType::RayTracingAccelStruct,
					        R.mGeometry,
					        {},
					        EArdaDependencyAccess::Read,
					        {});
					    !S)
					{
						return S;
					}
				}
			}
		}
		return {};
	}
}
