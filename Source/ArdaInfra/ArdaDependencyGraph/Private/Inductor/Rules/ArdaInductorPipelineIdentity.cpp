#include "Inductor/Rules/ArdaInductorPipelineInternal.h"

namespace arda::pipeline_detail
{
	FArdaPipelineKeyWriter ConfigurationKey(const FArdaInductorPipelineConfiguration& C, bool bStages)
	{
		FArdaPipelineKeyWriter W;
		W.Add(0x4152444150534f01ull); // Semantic schema version.
		W.Add(C.mKind);
		switch (C.mKind)
		{
		case EArdaPipelineStateKind::Compute:
			if (bStages)
			{
				W.Shader(C.mCompute.mDesc.mComputeShader);
				W.Layouts(C.mCompute.mDesc.mBindingLayouts);
			}
			break;
		case EArdaPipelineStateKind::Graphics:
		{
			const auto& D = C.mGraphics.mDesc;
			W.Raster(D);
			W.Add(D.mPatchControlPoints);
			W.Input(D.mInputLayout);
			if (bStages)
			{
				W.Shader(D.mVertexShader);
				W.Shader(D.mHullShader);
				W.Shader(D.mDomainShader);
				W.Shader(D.mGeometryShader);
				W.Shader(D.mPixelShader);
				W.Layouts(D.mBindingLayouts);
			}
			break;
		}
		case EArdaPipelineStateKind::Meshlet:
		{
			const auto& D = C.mMeshlet.mDesc;
			W.Raster(D);
			if (bStages)
			{
				W.Shader(D.mAmplificationShader);
				W.Shader(D.mMeshShader);
				W.Shader(D.mPixelShader);
				W.Layouts(D.mBindingLayouts);
			}
			break;
		}
		case EArdaPipelineStateKind::RayTracing:
		{
			const auto& D = C.mRayTracing.mDesc;
			W.Add(D.mMaxPayloadSize);
			W.Add(D.mMaxAttributeSize);
			W.Add(D.mMaxRecursionDepth);
			W.Add(D.mbAllowOpacityMicromaps);
			if (bStages)
			{
				W.Layouts(D.mGlobalBindingLayouts);
				W.Add(D.mShaders.size());
				for (const auto& S : D.mShaders)
				{
					W.String(S.mExportName);
					W.Shader(S.mShader);
					W.Layout(S.mLocalBindingLayout);
				}
				W.Add(D.mHitGroups.size());
				for (const auto& G : D.mHitGroups)
				{
					W.String(G.mExportName);
					W.Shader(G.mClosestHitShader);
					W.Shader(G.mAnyHitShader);
					W.Shader(G.mIntersectionShader);
					W.Layout(G.mLocalBindingLayout);
					W.Add(G.mbProceduralPrimitive);
				}
			}
			break;
		}
		case EArdaPipelineStateKind::WorkGraph:
		{
			const auto& D = C.mWorkGraph.mDesc;
			W.String(D.mProgramName);
			W.String(D.mEntryPoint);
			W.Add(D.mMaxInputRecords);
			if (bStages)
			{
				W.Layouts(D.mGlobalBindingLayouts);
				W.Add(D.mShaders.size());
				for (const auto& S : D.mShaders)
				{
					W.Shader(S);
				}
			}
			break;
		}
		default:
			W.mbValid = false;
			break;
		}
		return W;
	}
}
