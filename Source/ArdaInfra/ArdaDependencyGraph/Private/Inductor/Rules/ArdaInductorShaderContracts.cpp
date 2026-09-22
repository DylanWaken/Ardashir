#include "Inductor/Rules/ArdaInductorShaderContracts.h"
#include "Inductor/Rules/ArdaInductorBindingArguments.h"
#include <EASTL/algorithm.h>

namespace arda
{
	namespace
	{
		struct FArdaShaderContract
		{
			FArdaRHIShaderRef mShader;
			FArdaRHIBindingLayoutRef mLocalLayout;
			eastl::string mExportName;
		};

		const eastl::vector<FArdaRHIBindingLayoutRef>& GetLayouts(const FArdaInductorPipelineConfiguration& C)
		{
			switch (C.mKind)
			{
			case EArdaPipelineStateKind::Compute:
				return C.mCompute.mDesc.mBindingLayouts;
			case EArdaPipelineStateKind::Graphics:
				return C.mGraphics.mDesc.mBindingLayouts;
			case EArdaPipelineStateKind::Meshlet:
				return C.mMeshlet.mDesc.mBindingLayouts;
			case EArdaPipelineStateKind::RayTracing:
				return C.mRayTracing.mDesc.mGlobalBindingLayouts;
			default:
				return C.mWorkGraph.mDesc.mGlobalBindingLayouts;
			}
		}

		eastl::vector<FArdaShaderContract> GetShaders(const FArdaInductorPipelineConfiguration& C)
		{
			eastl::vector<FArdaShaderContract> Result;
			const auto Add = [&](const FArdaRHIShaderRef& Shader,
			                     const FArdaRHIBindingLayoutRef& Local = {},
			                     const eastl::string& Export = {})
			{
				if (Shader)
				{
					Result.push_back({Shader, Local, Export});
				}
			};
			switch (C.mKind)
			{
			case EArdaPipelineStateKind::Compute:
				Add(C.mCompute.mDesc.mComputeShader);
				break;
			case EArdaPipelineStateKind::Graphics:
				Add(C.mGraphics.mDesc.mVertexShader);
				Add(C.mGraphics.mDesc.mHullShader);
				Add(C.mGraphics.mDesc.mDomainShader);
				Add(C.mGraphics.mDesc.mGeometryShader);
				Add(C.mGraphics.mDesc.mPixelShader);
				break;
			case EArdaPipelineStateKind::Meshlet:
				Add(C.mMeshlet.mDesc.mAmplificationShader);
				Add(C.mMeshlet.mDesc.mMeshShader);
				Add(C.mMeshlet.mDesc.mPixelShader);
				break;
			case EArdaPipelineStateKind::RayTracing:
				for (const auto& Shader : C.mRayTracing.mDesc.mShaders)
				{
					Add(Shader.mShader, Shader.mLocalBindingLayout, Shader.mExportName);
				}
				for (const auto& Hit : C.mRayTracing.mDesc.mHitGroups)
				{
					Add(Hit.mClosestHitShader, Hit.mLocalBindingLayout, Hit.mExportName);
					Add(Hit.mAnyHitShader, Hit.mLocalBindingLayout, Hit.mExportName);
					Add(Hit.mIntersectionShader, Hit.mLocalBindingLayout, Hit.mExportName);
				}
				break;
			default:
				for (const auto& Shader : C.mWorkGraph.mDesc.mShaders)
				{
					Add(Shader);
				}
				break;
			}
			return Result;
		}

		bool Matches(const FArdaRHIBindingLayoutRef& Layout, const FArdaRHIBindingLayoutDesc& Expected)
		{
			return Layout && !Layout->GetBindlessDesc() && Layout->GetDesc() == Expected;
		}

		bool SameBinding(const FArdaShaderParameterMember& A, const FArdaShaderParameterMember& B)
		{
			return A.mBindingType == B.mBindingType && A.mSlot == B.mSlot && A.mRegisterSpace == B.mRegisterSpace &&
			    A.mArrayCount == B.mArrayCount && A.mVisibility == B.mVisibility;
		}

		bool Covers(EArdaDependencyAccess Actual, EArdaShaderResourceAccess Required)
		{
			return Actual == EArdaDependencyAccess::ReadWrite ||
			    (Actual == EArdaDependencyAccess::Read && Required == EArdaShaderResourceAccess::Read) ||
			    (Actual == EArdaDependencyAccess::Write && Required == EArdaShaderResourceAccess::Write);
		}

		FArdaRHIStatus ValidateMember(const FArdaShaderParameterMember& Required,
		    const FArdaDependencyShaderBindings& Bindings,
		    bool& Found)
		{
			if (!Bindings.mMetadata)
			{
				return {};
			}
			eastl::vector<FArdaFlattenedShaderParameterMember> Members;
			Bindings.mMetadata->GetFlattenedMembers(Members);
			for (const auto& Member : Members)
			{
				if (!SameBinding(*Member.mMember, Required))
				{
					continue;
				}
				for (uint32_t E = 0; E < Required.mArrayCount; ++E)
				{
					const auto* Argument = FindArgument(Bindings, Member.mPath, E);
					if (!Argument)
					{
						return BindingError("Node shader contract is missing a required resource argument.");
					}
					auto Access = Argument->mAccess;
					if (Access == EArdaDependencyAccess::AutoDeduction)
					{
						const auto Declared = Member.mMember->GetResourceAccess();
						Access = Declared == EArdaShaderResourceAccess::Read ? EArdaDependencyAccess::Read
						    : Declared == EArdaShaderResourceAccess::Write   ? EArdaDependencyAccess::Write
						                                                     : EArdaDependencyAccess::ReadWrite;
					}
					if (!Covers(Access, Required.GetResourceAccess()))
					{
						return BindingError("Node resource access is narrower than the prepared shader requirement.");
					}
				}
				Found = true;
			}
			return {};
		}
	}

	FArdaRHIStatus ValidateArdaPipelineShaderContract(const FArdaInductorPipelineConfiguration& C)
	{
		const auto Shaders = GetShaders(C);
		const auto& Layouts = GetLayouts(C);
		eastl::vector<FArdaRHIBindingLayoutDesc> Required;
		EArdaRHIShaderStage UnknownStages = EArdaRHIShaderStage::None;
		for (const auto& Shader : Shaders)
		{
			const auto* Metadata = Shader.mShader->GetParameterMetadata();
			if (!Metadata)
			{
				UnknownStages |= Shader.mShader->GetStage();
				continue;
			}
			eastl::vector<FArdaRHIBindingLayoutDesc> Expected;
			if (auto Status = Metadata->BuildBindingLayoutDescs(Expected); !Status)
			{
				return BindingError(Status.mMessage);
			}
			for (const auto& Layout : Expected)
			{
				if (!HasAnyFlags(Layout.mVisibility, Shader.mShader->GetStage()))
				{
					return BindingError("Shader parameter visibility does not include the prepared shader stage.");
				}
				if (!Matches(Shader.mLocalLayout, Layout))
				{
					if (!eastl::any_of(Layouts.begin(),
					        Layouts.end(),
					        [&](const auto& L)
					        {
						        return Matches(L, Layout);
					        }))
					{
						return BindingError(
						    "Prepared shader parameter contract is missing or mismatches a pipeline layout.");
					}
					Required.push_back(Layout);
				}
			}
			if (Shader.mLocalLayout && !Shader.mLocalLayout->GetBindlessDesc() &&
			    !eastl::any_of(Expected.begin(),
			        Expected.end(),
			        [&](const auto& L)
			        {
				        return Matches(Shader.mLocalLayout, L);
			        }))
			{
				return BindingError("Ray local layout is not declared by its prepared shader parameter contract.");
			}
		}
		for (const auto& Layout : Layouts)
		{
			if (!Layout)
			{
				return BindingError("Null pipeline layout in shader contract.");
			}
			// Bindless tables have their own explicit contract; fixed shader metadata does not describe them.
			if (Layout->GetBindlessDesc() || HasAnyFlags(Layout->GetDesc().mVisibility, UnknownStages))
			{
				continue;
			}
			if (!eastl::any_of(Required.begin(),
			        Required.end(),
			        [&](const auto& L)
			        {
				        return Matches(Layout, L);
			        }))
			{
				return BindingError(
				    "Pipeline contains a fixed binding layout absent from its prepared shader contracts.");
			}
		}
		return {};
	}

	FArdaRHIStatus ValidateArdaNodeShaderContract(const FArdaDependencyNodeDesc& D,
	    const FArdaInductorPipelineConfiguration& C,
	    const eastl::string& Slot)
	{
		const auto InSlot = [&](const auto& B)
		{
			return (B.mPipelineSlot.empty() ? eastl::string("default") : B.mPipelineSlot) == Slot;
		};
		if (!D.mbDeclarativeParameters && !eastl::any_of(D.mShaderBindings.begin(), D.mShaderBindings.end(), InSlot) &&
		    !eastl::any_of(D.mBindlessTables.begin(), D.mBindlessTables.end(), InSlot) &&
		    !eastl::any_of(D.mShaderTables.begin(), D.mShaderTables.end(), InSlot))
		{
			return {};
		}
		for (const auto& Layout : GetLayouts(C))
		{
			bool Found = false;
			if (Layout->GetBindlessDesc())
			{
				Found = eastl::any_of(D.mBindlessTables.begin(),
				    D.mBindlessTables.end(),
				    [&](const auto& B)
				    {
					    return InSlot(B) && B.mLayout == *Layout->GetBindlessDesc();
				    });
			}
			else
			{
				for (const auto& B : D.mShaderBindings)
				{
					if (!InSlot(B) || !B.mMetadata)
					{
						continue;
					}
					eastl::vector<FArdaRHIBindingLayoutDesc> Declared;
					if (auto Status = B.mMetadata->BuildBindingLayoutDescs(Declared); !Status)
					{
						return BindingError(Status.mMessage);
					}
					Found |= eastl::any_of(Declared.begin(),
					    Declared.end(),
					    [&](const auto& L)
					    {
						    return Matches(Layout, L);
					    });
				}
			}
			if (!Found)
			{
				return BindingError(
				    "Prepared pipeline requires a shader parameter layout with no node argument schema.");
			}
		}
		for (const auto& Shader : GetShaders(C))
		{
			const auto* Metadata = Shader.mShader->GetParameterMetadata();
			if (!Metadata)
			{
				continue;
			}
			eastl::vector<FArdaFlattenedShaderParameterMember> Required;
			Metadata->GetFlattenedMembers(Required);
			for (const auto& Requirement : Required)
			{
				const auto& M = *Requirement.mMember;
				if (M.mKind == EArdaShaderParameterKind::Value || M.mKind == EArdaShaderParameterKind::PushConstants)
				{
					continue;
				}
				bool Found = false;
				if (Shader.mLocalLayout && Shader.mLocalLayout->GetDesc().mRegisterSpace == M.mRegisterSpace &&
				    Shader.mLocalLayout->GetDesc().mVisibility == M.mVisibility)
				{
					for (const auto& Table : D.mShaderTables)
					{
						if (!InSlot(Table))
						{
							continue;
						}
						for (const auto& Record : Table.mRecords)
						{
							if (Record.mExportName != Shader.mExportName)
							{
								continue;
							}
							bool RecordFound = false;
							if (auto Status = ValidateMember(M, Record.mLocalBindings, RecordFound); !Status)
							{
								return Status;
							}
							if (!RecordFound)
							{
								return BindingError("Ray record omits a prepared shader resource parameter.");
							}
							Found = true;
						}
					}
				}
				else
				{
					for (const auto& B : D.mShaderBindings)
					{
						if (!InSlot(B))
						{
							continue;
						}
						if (auto Status = ValidateMember(M, B, Found); !Status)
						{
							return Status;
						}
					}
				}
				if (!Found)
				{
					return BindingError("Prepared shader resource has no matching node parameter declaration.");
				}
			}
		}
		return {};
	}
}
