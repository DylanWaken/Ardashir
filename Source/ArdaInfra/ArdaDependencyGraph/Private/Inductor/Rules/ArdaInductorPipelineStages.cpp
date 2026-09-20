#include "Inductor/Rules/ArdaInductorPipelineInternal.h"

namespace arda::pipeline_detail
{
	bool SameShader(const FArdaRHIShaderRef& A, const FArdaRHIShaderRef& B)
	{
		return A == B ||
		    (A && B && A->GetStage() == B->GetStage() && A->GetPersistentCacheHash() != 0 &&
		        A->GetPersistentCacheHash() == B->GetPersistentCacheHash());
	}

	FArdaRHIStatus AssignShader(FArdaRHIShaderRef& Destination,
	    const FArdaRHIShaderRef& Source,
	    EArdaRHIShaderStage Expected)
	{
		if (!Source)
		{
			return {};
		}
		if (Source->GetStage() != Expected || !Source->GetPersistentCacheHash())
		{
			return Invalid("Pipeline shaders require the declared stage and a deterministic content hash.");
		}
		if (Destination && !SameShader(Destination, Source))
		{
			return Invalid(
			    "Pipeline inference found conflicting shaders for one stage or export; select a unique group.");
		}
		Destination = Source;
		return {};
	}

	bool Matches(EArdaPipelineStateKind Kind, EArdaRHIShaderStage Stage)
	{
		switch (Kind)
		{
		case EArdaPipelineStateKind::Compute:
			return Stage == EArdaRHIShaderStage::Compute;
		case EArdaPipelineStateKind::Graphics:
			return Stage == EArdaRHIShaderStage::Vertex || Stage == EArdaRHIShaderStage::Hull ||
			    Stage == EArdaRHIShaderStage::Domain || Stage == EArdaRHIShaderStage::Geometry ||
			    Stage == EArdaRHIShaderStage::Pixel;
		case EArdaPipelineStateKind::Meshlet:
			return Stage == EArdaRHIShaderStage::Amplification || Stage == EArdaRHIShaderStage::Mesh ||
			    Stage == EArdaRHIShaderStage::Pixel;
		case EArdaPipelineStateKind::RayTracing:
			return IsArdaRHIRayTracingShaderStage(Stage);
		case EArdaPipelineStateKind::WorkGraph:
			return Stage == EArdaRHIShaderStage::WorkGraph;
		default:
			return false;
		}
	}

	FArdaRHIStatus AddShader(FArdaInductorPipelineConfiguration& C, const FArdaInductorPipelineContribution& S)
	{
		if (!S.mShader)
		{
			return S.mConfiguration ? FArdaRHIStatus{}
			                        : Invalid("A pipeline stage contribution requires a shader or configuration.");
		}
		const auto Stage = S.mShader->GetStage();
		if (!Matches(C.mKind, Stage) || !S.mShader->GetPersistentCacheHash())
		{
			return Invalid("A pipeline shader has an incompatible stage or lacks a deterministic content hash.");
		}
		switch (C.mKind)
		{
		case EArdaPipelineStateKind::Compute:
			return AssignShader(C.mCompute.mDesc.mComputeShader, S.mShader, Stage);
		case EArdaPipelineStateKind::Graphics:
			switch (Stage)
			{
			case EArdaRHIShaderStage::Vertex:
				return AssignShader(C.mGraphics.mDesc.mVertexShader, S.mShader, Stage);
			case EArdaRHIShaderStage::Hull:
				return AssignShader(C.mGraphics.mDesc.mHullShader, S.mShader, Stage);
			case EArdaRHIShaderStage::Domain:
				return AssignShader(C.mGraphics.mDesc.mDomainShader, S.mShader, Stage);
			case EArdaRHIShaderStage::Geometry:
				return AssignShader(C.mGraphics.mDesc.mGeometryShader, S.mShader, Stage);
			default:
				return AssignShader(C.mGraphics.mDesc.mPixelShader, S.mShader, Stage);
			}
		case EArdaPipelineStateKind::Meshlet:
			if (Stage == EArdaRHIShaderStage::Amplification)
			{
				return AssignShader(C.mMeshlet.mDesc.mAmplificationShader, S.mShader, Stage);
			}
			if (Stage == EArdaRHIShaderStage::Mesh)
			{
				return AssignShader(C.mMeshlet.mDesc.mMeshShader, S.mShader, Stage);
			}
			return AssignShader(C.mMeshlet.mDesc.mPixelShader, S.mShader, Stage);
		case EArdaPipelineStateKind::WorkGraph:
			if (eastl::none_of(C.mWorkGraph.mDesc.mShaders.begin(),
			        C.mWorkGraph.mDesc.mShaders.end(),
			        [&](const auto& Other)
			        {
				        return SameShader(Other, S.mShader);
			        }))
			{
				C.mWorkGraph.mDesc.mShaders.push_back(S.mShader);
			}
			return {};
		case EArdaPipelineStateKind::RayTracing:
		{
			auto& D = C.mRayTracing.mDesc;
			if (Stage == EArdaRHIShaderStage::ClosestHit || Stage == EArdaRHIShaderStage::AnyHit ||
			    Stage == EArdaRHIShaderStage::Intersection)
			{
				if (S.mHitGroupName.empty())
				{
					return Invalid("Hit-stage contributions require an explicit hit-group name.");
				}
				auto Existing = eastl::find_if(D.mHitGroups.begin(),
				    D.mHitGroups.end(),
				    [&](const auto& G)
				    {
					    return G.mExportName == S.mHitGroupName;
				    });
				if (Existing == D.mHitGroups.end())
				{
					D.mHitGroups.push_back({});
					Existing = D.mHitGroups.end() - 1;
					Existing->mExportName = S.mHitGroupName;
				}
				if (auto Status = MergeLocalLayout(Existing->mLocalBindingLayout, S.mLocalBindingLayout); !Status)
				{
					return Status;
				}
				if (Stage == EArdaRHIShaderStage::ClosestHit)
				{
					return AssignShader(Existing->mClosestHitShader, S.mShader, Stage);
				}
				if (Stage == EArdaRHIShaderStage::AnyHit)
				{
					return AssignShader(Existing->mAnyHitShader, S.mShader, Stage);
				}
				Existing->mbProceduralPrimitive = true;
				return AssignShader(Existing->mIntersectionShader, S.mShader, Stage);
			}
			if (S.mExportName.empty() || !S.mHitGroupName.empty())
			{
				return Invalid("Ray-generation, miss and callable contributions require a general export name.");
			}
			auto Existing = eastl::find_if(D.mShaders.begin(),
			    D.mShaders.end(),
			    [&](const auto& Export)
			    {
				    return Export.mExportName == S.mExportName;
			    });
			if (Existing == D.mShaders.end())
			{
				D.mShaders.push_back({S.mExportName, S.mShader, S.mLocalBindingLayout});
				return {};
			}
			if (auto Status = MergeLocalLayout(Existing->mLocalBindingLayout, S.mLocalBindingLayout); !Status)
			{
				return Status;
			}
			return AssignShader(Existing->mShader, S.mShader, Stage);
		}
		default:
			return Invalid("Unknown pipeline kind.");
		}
	}
}
