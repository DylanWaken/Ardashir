#include "Inductor/Rules/ArdaInductorPipelineInternal.h"

namespace arda::pipeline_detail
{
	void ClearStages(FArdaInductorPipelineConfiguration& C)
	{
		GlobalLayouts(C).clear();
		C.mCompute.mDesc.mComputeShader.Reset();
		auto& G = C.mGraphics.mDesc;
		G.mVertexShader.Reset();
		G.mHullShader.Reset();
		G.mDomainShader.Reset();
		G.mGeometryShader.Reset();
		G.mPixelShader.Reset();
		auto& M = C.mMeshlet.mDesc;
		M.mAmplificationShader.Reset();
		M.mMeshShader.Reset();
		M.mPixelShader.Reset();
		C.mRayTracing.mDesc.mShaders.clear();
		C.mRayTracing.mDesc.mHitGroups.clear();
		C.mWorkGraph.mDesc.mShaders.clear();
	}

	FArdaRHIStatus MergeConfiguration(FArdaInductorPipelineConfiguration& Out,
	    bool& bHaveSettings,
	    const FArdaInductorPipelineConfiguration& Source)
	{
		if (Out.mKind != Source.mKind)
		{
			return Invalid("A pipeline configuration disagrees with the requested pipeline kind.");
		}
		if (bHaveSettings && ConfigurationKey(Out, false).mBytes != ConfigurationKey(Source, false).mBytes)
		{
			return Invalid("Reachable pipeline stages supply incompatible fixed-function or program settings.");
		}
		if (!bHaveSettings)
		{
			Out = Source;
			ClearStages(Out);
			bHaveSettings = true;
		}
		auto Copy = Source;
		if (auto Status = MergeLayouts(GlobalLayouts(Out), GlobalLayouts(Copy)); !Status)
		{
			return Status;
		}
		auto Add = [&](const FArdaRHIShaderRef& Shader,
		               EArdaRHIShaderStage Expected,
		               const eastl::string& Export = {},
		               const eastl::string& Hit = {},
		               const FArdaRHIBindingLayoutRef& Local = {}) -> FArdaRHIStatus
		{
			if (!Shader)
			{
				return {};
			}
			if (Shader->GetStage() != Expected)
			{
				return Invalid("A pipeline configuration puts a shader in the wrong stage.");
			}
			FArdaInductorPipelineContribution C;
			C.mShader = Shader;
			C.mExportName = Export;
			C.mHitGroupName = Hit;
			C.mLocalBindingLayout = Local;
			return AddShader(Out, C);
		};
		switch (Out.mKind)
		{
		case EArdaPipelineStateKind::Compute:
			return Add(Source.mCompute.mDesc.mComputeShader, EArdaRHIShaderStage::Compute);
		case EArdaPipelineStateKind::Graphics:
		{
			const auto& D = Source.mGraphics.mDesc;
			for (const auto& Pair :
			    {eastl::pair<FArdaRHIShaderRef, EArdaRHIShaderStage>{D.mVertexShader, EArdaRHIShaderStage::Vertex},
			        {D.mHullShader, EArdaRHIShaderStage::Hull},
			        {D.mDomainShader, EArdaRHIShaderStage::Domain},
			        {D.mGeometryShader, EArdaRHIShaderStage::Geometry},
			        {D.mPixelShader, EArdaRHIShaderStage::Pixel}})
			{
				if (auto Status = Add(Pair.first, Pair.second); !Status)
				{
					return Status;
				}
			}
			break;
		}
		case EArdaPipelineStateKind::Meshlet:
		{
			const auto& D = Source.mMeshlet.mDesc;
			for (const auto& Pair : {eastl::pair<FArdaRHIShaderRef, EArdaRHIShaderStage>{D.mAmplificationShader,
			                             EArdaRHIShaderStage::Amplification},
			         {D.mMeshShader, EArdaRHIShaderStage::Mesh},
			         {D.mPixelShader, EArdaRHIShaderStage::Pixel}})
			{
				if (auto Status = Add(Pair.first, Pair.second); !Status)
				{
					return Status;
				}
			}
			break;
		}
		case EArdaPipelineStateKind::RayTracing:
			for (const auto& S : Source.mRayTracing.mDesc.mShaders)
			{
				if (!S.mShader)
				{
					return Invalid("A ray export cannot contain a null shader.");
				}
				if (auto Status = Add(S.mShader, S.mShader->GetStage(), S.mExportName, {}, S.mLocalBindingLayout);
				    !Status)
				{
					return Status;
				}
			}
			for (const auto& G : Source.mRayTracing.mDesc.mHitGroups)
			{
				if (G.mExportName.empty() || (!G.mClosestHitShader && !G.mAnyHitShader && !G.mIntersectionShader))
				{
					return Invalid("A configured ray hit group requires a name and at least one shader.");
				}
				if (G.mbProceduralPrimitive != bool(G.mIntersectionShader))
				{
					return Invalid("Procedural hit-group settings must match the intersection shader.");
				}
				for (const auto& Pair : {eastl::pair<FArdaRHIShaderRef, EArdaRHIShaderStage>{G.mClosestHitShader,
				                             EArdaRHIShaderStage::ClosestHit},
				         {G.mAnyHitShader, EArdaRHIShaderStage::AnyHit},
				         {G.mIntersectionShader, EArdaRHIShaderStage::Intersection}})
				{
					if (auto Status = Add(Pair.first, Pair.second, {}, G.mExportName, G.mLocalBindingLayout); !Status)
					{
						return Status;
					}
				}
			}
			break;
		case EArdaPipelineStateKind::WorkGraph:
			for (const auto& S : Source.mWorkGraph.mDesc.mShaders)
			{
				if (!S)
				{
					return Invalid("A work-graph configuration cannot contain a null shader.");
				}
				if (auto Status = Add(S, EArdaRHIShaderStage::WorkGraph); !Status)
				{
					return Status;
				}
			}
			break;
		default:
			return Invalid("Unknown pipeline kind.");
		}
		return {};
	}
}
