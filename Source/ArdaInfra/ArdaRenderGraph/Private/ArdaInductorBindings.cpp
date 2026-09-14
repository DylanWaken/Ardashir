#include "ArdaInductorPch.h"
#include "ArdaDependencyGraphInternal.h"
#include <EASTL/algorithm.h>
#include <cstring>

namespace arda
{
	namespace
	{
		FArdaRHIStatus BindingError(const eastl::string& Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message.c_str());
		}

		const FArdaDependencyShaderResource* FindArgument(const FArdaDependencyShaderBindings& B,
		    const eastl::string& Path,
		    uint32_t E)
		{
			for (const auto& R : B.mResources)
			{
				if (R.mMember == Path && R.mArrayElement == E)
				{
					return &R;
				}
			}
			return nullptr;
		}

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

		const eastl::vector<FArdaRHIBindingLayoutRef>& PipelineLayouts(const FArdaInductorResolvedPipeline& P)
		{
			if (P.mCompute)
			{
				return P.mCompute->GetDesc().mBindingLayouts;
			}
			if (P.mGraphics)
			{
				return P.mGraphics->GetDesc().mBindingLayouts;
			}
			if (P.mMeshlet)
			{
				return P.mMeshlet->GetDesc().mBindingLayouts;
			}
			if (P.mRayTracing)
			{
				return P.mRayTracing->GetDesc().mGlobalBindingLayouts;
			}
			if (P.mWorkGraph)
			{
				return P.mWorkGraph->GetDesc().mGlobalBindingLayouts;
			}
			static const eastl::vector<FArdaRHIBindingLayoutRef> Empty;
			return Empty;
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
					if (auto S =
					        DescribeResource(G, D, M.mBindingType, R->mResource, R->mView, R->mAccess, R->mSampler);
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

		FArdaRHIResourceRef Physical(const FArdaDependencyGraph::FArdaImpl& G,
		    FArdaInductorFrame& Frame,
		    FArdaDependencyResourceHandle R,
		    const FArdaRHISamplerRef& Sampler)
		{
			if (Sampler)
			{
				return FArdaRHIResourceRef(Sampler.Get());
			}
			const auto& D = G.mResources[R.mIndex];
			if (D.mbTexture)
			{
				return FArdaRHIResourceRef(Frame.mMemory.mTextures[R.mIndex].Get());
			}
			if (D.mExternalAccelerationStructure)
			{
				return FArdaRHIResourceRef(Frame.mMemory.mAccelerationStructures[R.mIndex].Get());
			}
			return FArdaRHIResourceRef(Frame.mMemory.mBuffers[R.mIndex].Get());
		}

		bool MatchesSchema(const FArdaDependencyShaderBindings& B, const FArdaRHIBindingLayoutRef& Layout)
		{
			if (!B.mMetadata || !Layout || Layout->GetBindlessDesc())
			{
				return false;
			}
			eastl::vector<FArdaRHIBindingLayoutDesc> Generated;
			if (!B.mMetadata->BuildBindingLayoutDescs(Generated))
			{
				return false;
			}
			return eastl::any_of(Generated.begin(),
			    Generated.end(),
			    [&](const auto& D)
			    {
				    return D == Layout->GetDesc();
			    });
		}

		FArdaRHIBindingSetDesc MakeSet(const FArdaDependencyGraph::FArdaImpl& G,
		    FArdaInductorFrame& Frame,
		    const FArdaDependencyShaderBindings& B,
		    const FArdaRHIBindingLayoutRef& Layout)
		{
			FArdaRHIBindingSetDesc Set;
			Set.mLayout = Layout;
			eastl::vector<FArdaFlattenedShaderParameterMember> Members;
			B.mMetadata->GetFlattenedMembers(Members);
			for (const auto& F : Members)
			{
				const auto& M = *F.mMember;
				if (M.mKind == EArdaShaderParameterKind::Value || M.mKind == EArdaShaderParameterKind::PushConstants ||
				    M.mRegisterSpace != Layout->GetDesc().mRegisterSpace ||
				    M.mVisibility != Layout->GetDesc().mVisibility)
				{
					continue;
				}
				for (uint32_t E = 0; E < M.mArrayCount; ++E)
				{
					const auto* R = FindArgument(B, F.mPath, E);
					FArdaRHIBindingItem I;
					I.mSlot = M.mSlot;
					I.mArrayElement = E;
					I.mType = M.mBindingType;
					I.mView = R->mView;
					I.mResource = Physical(G, Frame, R->mResource, R->mSampler);
					Set.mItems.push_back(eastl::move(I));
				}
			}
			return Set;
		}

		bool SameItems(const FArdaRHIBindingSetDesc& A, const FArdaRHIBindingSetDesc& B)
		{
			if (A.mItems.size() != B.mItems.size())
			{
				return false;
			}
			for (const auto& I : A.mItems)
			{
				if (!eastl::any_of(B.mItems.begin(),
				        B.mItems.end(),
				        [&](const auto& J)
				        {
					        return I.mSlot == J.mSlot && I.mArrayElement == J.mArrayElement && I.mType == J.mType &&
					            I.mResource == J.mResource && I.mView == J.mView;
				        }))
				{
					return false;
				}
			}
			return true;
		}

		FArdaRHIStatus PrepareRayTable(const FArdaDependencyGraph::FArdaImpl& G,
		    FArdaInductorFrame& Frame,
		    uint32_t NodeId,
		    const FArdaDependencyNode& Node,
		    const eastl::string& Slot,
		    const FArdaRHIRayTracingPipelineRef& Pipeline)
		{
			eastl::vector<FArdaDependencyShaderTableRecord> Records;
			const auto T = eastl::find_if(Node.mDesc.mShaderTables.begin(),
			    Node.mDesc.mShaderTables.end(),
			    [&](const auto& Table)
			    {
				    return Table.mPipelineSlot == Slot;
			    });
			const auto& PD = Pipeline->GetDesc();
			if (T != Node.mDesc.mShaderTables.end())
			{
				Records = T->mRecords;
			}
			else
			{
				uint32_t Counts[4]{};
				for (const auto& S : PD.mShaders)
				{
					EArdaRHIShaderTableRecordType Type;
					switch (S.mShader->GetStage())
					{
					case EArdaRHIShaderStage::RayGeneration:
						Type = EArdaRHIShaderTableRecordType::RayGeneration;
						break;
					case EArdaRHIShaderStage::Miss:
						Type = EArdaRHIShaderTableRecordType::Miss;
						break;
					case EArdaRHIShaderStage::Callable:
						Type = EArdaRHIShaderTableRecordType::Callable;
						break;
					default:
						continue;
					}
					Records.push_back({Type, Counts[static_cast<uint32_t>(Type)]++, S.mExportName});
				}
				for (const auto& H : PD.mHitGroups)
				{
					Records.push_back({EArdaRHIShaderTableRecordType::HitGroup, Counts[2]++, H.mExportName});
				}
			}
			uint32_t Counts[4]{};
			FArdaRHIShaderTableDesc Desc;
			Desc.mbPersistent = true;
			Desc.mDebugName = Node.mName + "/" + Slot;
			Desc.mMaxEntries = static_cast<uint32_t>(Records.size());
			for (const auto& R : Records)
			{
				const auto Type = static_cast<uint32_t>(R.mType);
				if (Type >= 4 || R.mExportName.empty() || R.mLocalArguments.size() > UINT32_MAX)
				{
					return BindingError("Invalid shader table record.");
				}
				++Counts[Type];
				Desc.mMaxLocalArgumentBytes =
				    eastl::max(Desc.mMaxLocalArgumentBytes, static_cast<uint32_t>(R.mLocalArguments.size()));
			}
			if (Counts[0] != 1)
			{
				return BindingError(
				    "A shader table requires exactly one ray-generation record; declare an explicit selection.");
			}
			for (size_t I = 0; I < Records.size(); ++I)
			{
				const auto& R = Records[I];
				if (R.mRecordIndex >= Counts[static_cast<uint32_t>(R.mType)])
				{
					return BindingError("Shader table indices must be dense within each record category.");
				}
				for (size_t J = 0; J < I; ++J)
				{
					if (R.mType == Records[J].mType && R.mRecordIndex == Records[J].mRecordIndex)
					{
						return BindingError("Duplicate shader table record index.");
					}
				}
			}
			auto Created = G.mDevice->CreateShaderTable(Pipeline, Desc);
			if (!Created)
			{
				return Created.mStatus;
			}
			for (const auto& R : Records)
			{
				FArdaRHIShaderTableRecordDesc Native;
				Native.mType = R.mType;
				Native.mRecordIndex = R.mRecordIndex;
				Native.mExportName = R.mExportName;
				// Public node indices are category-relative; the RHI stores all categories in one record array.
				for (uint32_t Type = 0; Type < static_cast<uint32_t>(R.mType); ++Type)
				{
					Native.mRecordIndex += Counts[Type];
				}
				Native.mLocalArguments = R.mLocalArguments;
				Native.mUserData = R.mUserData;
				Native.mGeometrySegment = R.mGeometrySegment;
				if (R.mGeometry)
				{
					Native.mGeometry = Frame.mMemory.mAccelerationStructures[R.mGeometry.mIndex];
				}
				FArdaRHIBindingLayoutRef Local;
				bool Found = false;
				if (R.mType == EArdaRHIShaderTableRecordType::HitGroup)
				{
					for (const auto& H : PD.mHitGroups)
					{
						if (H.mExportName == R.mExportName)
						{
							Found = true;
							Local = H.mLocalBindingLayout;
						}
					}
				}
				else
				{
					const auto Stage = R.mType == EArdaRHIShaderTableRecordType::RayGeneration
					    ? EArdaRHIShaderStage::RayGeneration
					    : R.mType == EArdaRHIShaderTableRecordType::Miss ? EArdaRHIShaderStage::Miss
					                                                     : EArdaRHIShaderStage::Callable;
					for (const auto& S : PD.mShaders)
					{
						if (S.mExportName == R.mExportName && S.mShader->GetStage() == Stage)
						{
							Found = true;
							Local = S.mLocalBindingLayout;
						}
					}
				}
				if (!Found)
				{
					return BindingError("Shader table record names a missing export or the wrong shader stage.");
				}
				if (R.mLocalBindings.mMetadata)
				{
					if (!MatchesSchema(R.mLocalBindings, Local))
					{
						return BindingError("Ray local shader schema does not match its export's layout.");
					}
					auto Set = G.mDevice->CreateBindingSet(MakeSet(G, Frame, R.mLocalBindings, Local));
					if (!Set)
					{
						return Set.mStatus;
					}
					Native.mBindings = eastl::move(Set.mValue);
				}
				else if (Local && !Local->GetDesc().mItems.empty())
				{
					return BindingError("Shader table export needs declared local binding parameters.");
				}
				if (auto S = G.mDevice->SetShaderTableRecord(Created.mValue, Native); !S)
				{
					return S;
				}
			}
			if (auto S = G.mDevice->CommitShaderTable(Created.mValue); !S)
			{
				return S;
			}
			Frame.mShaderTables[NodeId][Slot] = eastl::move(Created.mValue);
			return {};
		}
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

	FArdaRHIStatus PrepareArdaShaderBindings(const FArdaDependencyGraph::FArdaImpl& G, FArdaInductorFrame& Frame)
	{
		for (auto Handle : G.mCompile.mExecutionOrder)
		{
			const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			const auto Slots = Frame.mPipelines.find(Handle.GetIndex());
			if (Slots == Frame.mPipelines.end())
			{
				if (!Node.mDesc.mShaderBindings.empty() || !Node.mDesc.mBindlessTables.empty() ||
				    !Node.mDesc.mShaderTables.empty())
				{
					return BindingError("No compiled pipeline for declared shader arguments.");
				}
				continue;
			}

			// Prepare each resolved pipeline slot independently for this retained frame allocation.
			for (const auto& Slot : Slots->second)
			{
				const auto& Layouts = PipelineLayouts(Slot.second);
				const bool Automatic = eastl::any_of(Node.mDesc.mShaderBindings.begin(),
				                           Node.mDesc.mShaderBindings.end(),
				                           [&](const auto& B)
				                           {
					                           return B.mPipelineSlot == Slot.first;
				                           }) ||
				    eastl::any_of(Node.mDesc.mBindlessTables.begin(),
				        Node.mDesc.mBindlessTables.end(),
				        [&](const auto& B)
				        {
					        return B.mPipelineSlot == Slot.first;
				        });
				if (Automatic)
				{
					for (const auto& B : Node.mDesc.mShaderBindings)
					{
						if (B.mPipelineSlot != Slot.first)
						{
							continue;
						}
						eastl::vector<FArdaRHIBindingLayoutDesc> Generated;
						if (auto S = B.mMetadata->BuildBindingLayoutDescs(Generated); !S)
						{
							return BindingError(S.mMessage);
						}
						for (const auto& D : Generated)
						{
							if (!eastl::any_of(Layouts.begin(),
							        Layouts.end(),
							        [&](const auto& L)
							        {
								        return L && !L->GetBindlessDesc() && L->GetDesc() == D;
							        }))
							{
								return BindingError("Shader schema has a layout absent from the compiled pipeline.");
							}
						}
					}

					// Materialize fixed sets and bindless table entries from this frame's physical resources.
					auto& Sets = Frame.mBindings[Handle.GetIndex()][Slot.first];
					for (const auto& Layout : Layouts)
					{
						if (!Layout)
						{
							return BindingError("Null compiled pipeline layout.");
						}
						if (Layout->GetBindlessDesc())
						{
							const auto B = eastl::find_if(Node.mDesc.mBindlessTables.begin(),
							    Node.mDesc.mBindlessTables.end(),
							    [&](const auto& T)
							    {
								    return T.mPipelineSlot == Slot.first &&
								        T.mLayout.mRegisterSpace == Layout->GetDesc().mRegisterSpace;
							    });
							if (B == Node.mDesc.mBindlessTables.end())
							{
								return BindingError("Pipeline bindless layout has no declared table.");
							}
							auto Table = G.mDevice->CreateDescriptorTable(Layout);
							if (!Table)
							{
								return Table.mStatus;
							}
							if (auto S = G.mDevice->ResizeDescriptorTable(Table.mValue, B->mCapacity); !S)
							{
								return S;
							}
							for (const auto& R : B->mEntries)
							{
								FArdaRHIBindingItem Item;
								Item.mSlot = R.mSlot;
								Item.mArrayElement = R.mArrayElement;
								Item.mType = R.mType;
								Item.mView = R.mView;
								Item.mResource = Physical(G, Frame, R.mResource, R.mSampler);
								if (auto S = G.mDevice->WriteDescriptorTable(Table.mValue, Item); !S)
								{
									return S;
								}
							}
							Sets.push_back(FArdaRHIBindingSetRef(Table.mValue.Get()));
							Frame.mDescriptorTables[Handle.GetIndex()][B->mName] = eastl::move(Table.mValue);
							continue;
						}
						FArdaRHIBindingSetDesc Set;
						bool Matched = false;
						for (const auto& B : Node.mDesc.mShaderBindings)
						{
							if (B.mPipelineSlot != Slot.first || !MatchesSchema(B, Layout))
							{
								continue;
							}
							auto Candidate = MakeSet(G, Frame, B, Layout);
							if (Matched && !SameItems(Set, Candidate))
							{
								return BindingError("Different shader arguments target the same pipeline layout.");
							}
							Set = eastl::move(Candidate);
							Matched = true;
						}
						if (!Matched)
						{
							return BindingError("Compiled pipeline layout has no matching shader parameter schema.");
						}
						Set.mDebugName = Node.mName;
						auto Created = G.mDevice->CreateBindingSet(Set);
						if (!Created)
						{
							return Created.mStatus;
						}
						Sets.push_back(eastl::move(Created.mValue));
					}
				}

				// Relocate logical descriptor references only after table storage and heap indices are known.
				bool HavePush = false;
				auto& Push = Frame.mShaderParameters[Handle.GetIndex()][Slot.first];
				for (const auto& B : Node.mDesc.mShaderBindings)
				{
					if (B.mPipelineSlot != Slot.first)
					{
						continue;
					}
					for (const auto& V : B.mValues)
					{
						auto Bytes = V.mBytes;
						for (size_t I = 0; I < V.mDescriptorIndices.size(); ++I)
						{
							const auto& Index = V.mDescriptorIndices[I];
							if (Index.mByteOffset % 4 || Index.mByteOffset > Bytes.size() ||
							    Bytes.size() - Index.mByteOffset < 4)
							{
								return BindingError(
								    "Descriptor index parameter exceeds or misaligns its uint32 field.");
							}
							for (size_t J = 0; J < I; ++J)
							{
								if (V.mDescriptorIndices[J].mByteOffset == Index.mByteOffset)
								{
									return BindingError("Duplicate descriptor-index parameter field.");
								}
							}
							const auto Table = eastl::find_if(Node.mDesc.mBindlessTables.begin(),
							    Node.mDesc.mBindlessTables.end(),
							    [&](const auto& T)
							    {
								    return T.mName == Index.mTable && T.mPipelineSlot == Slot.first;
							    });
							if (Table == Node.mDesc.mBindlessTables.end() ||
							    !eastl::any_of(Table->mEntries.begin(),
							        Table->mEntries.end(),
							        [&](const auto& E)
							        {
								        return E.mSlot == Index.mSlot && E.mType == Index.mType &&
								            E.mArrayElement == Index.mArrayElement;
							        }))
							{
								return BindingError("Descriptor index parameter names an undeclared table entry.");
							}
							uint64_t Value = Index.mArrayElement;
							if (Index.mbAbsoluteHeapIndex)
							{
								if (!Table->mLayout.mbDirectHeapIndexing || Table->mLayout.mRegisterSpaces.size() != 1)
								{
									return BindingError(
									    "Absolute indices require a direct-heap table with exactly one descriptor bank.");
								}
								Value += Frame.mDescriptorTables.at(Handle.GetIndex())
								             .at(Table->mName)
								             ->GetFirstDescriptorIndexInHeap();
							}
							if (Value > UINT32_MAX)
							{
								return BindingError("Descriptor heap index overflows uint32.");
							}
							const uint32_t NativeIndex = static_cast<uint32_t>(Value);
							std::memcpy(Bytes.data() + Index.mByteOffset, &NativeIndex, sizeof(NativeIndex));
						}
						if (HavePush && Push != Bytes)
						{
							return BindingError(
							    "A pipeline slot must supply identical bytes for its shared push-constant block.");
						}
						Push = eastl::move(Bytes);
						HavePush = true;
					}
				}

				// Build shader records after their pipeline identifiers and local arguments are available.
				if (Slot.second.mRayTracing)
				{
					if (auto S =
					        PrepareRayTable(G, Frame, Handle.GetIndex(), Node, Slot.first, Slot.second.mRayTracing);
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
