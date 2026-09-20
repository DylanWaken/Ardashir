#include "Inductor/Rules/ArdaInductorPipelineInternal.h"

namespace arda::pipeline_detail
{
	FArdaRHIStatus ValidatePattern(FArdaInductorPipelineConfiguration& C)
	{
		switch (C.mKind)
		{
		case EArdaPipelineStateKind::Compute:
			return Validate(C.mCompute.mDesc);
		case EArdaPipelineStateKind::Graphics:
		{
			auto D = C.mGraphics.mDesc;
			if (bool(D.mHullShader) != bool(D.mDomainShader) ||
			    (bool(D.mHullShader) != (D.mTopology == EArdaRHIPrimitiveTopology::PatchList)))
			{
				return Invalid("Tessellation requires paired hull/domain shaders and patch-list topology.");
			}
			if (!D.mSampleCount)
			{
				D.mSampleCount = 1; // Attachment-derived samples are validated at resolution.
			}
			return Validate(D);
		}
		case EArdaPipelineStateKind::Meshlet:
		{
			auto D = C.mMeshlet.mDesc;
			if (!D.mSampleCount)
			{
				D.mSampleCount = 1;
			}
			return Validate(D);
		}
		case EArdaPipelineStateKind::RayTracing:
		{
			auto& D = C.mRayTracing.mDesc;
			eastl::sort(D.mShaders.begin(),
			    D.mShaders.end(),
			    [](const auto& A, const auto& B)
			    {
				    return A.mExportName < B.mExportName;
			    });
			eastl::sort(D.mHitGroups.begin(),
			    D.mHitGroups.end(),
			    [](const auto& A, const auto& B)
			    {
				    return A.mExportName < B.mExportName;
			    });
			if (eastl::none_of(D.mShaders.begin(),
			        D.mShaders.end(),
			        [](const auto& S)
			        {
				        return S.mShader->GetStage() == EArdaRHIShaderStage::RayGeneration;
			        }))
			{
				return Invalid("A ray pipeline requires a ray-generation shader.");
			}
			for (const auto& G : D.mHitGroups)
			{
				if (eastl::any_of(D.mShaders.begin(),
				        D.mShaders.end(),
				        [&](const auto& S)
				        {
					        return S.mExportName == G.mExportName;
				        }))
				{
					return Invalid("Ray general exports and hit groups must have distinct names.");
				}
			}
			return Validate(D);
		}
		case EArdaPipelineStateKind::WorkGraph:
		{
			auto& D = C.mWorkGraph.mDesc;
			if (D.mProgramName.empty() || D.mEntryPoint.empty() || !D.mMaxInputRecords || D.mShaders.empty())
			{
				return Invalid(
				    "A work-graph pipeline requires program, entry point, shaders and non-zero record capacity.");
			}
			eastl::sort(D.mShaders.begin(),
			    D.mShaders.end(),
			    [](const auto& A, const auto& B)
			    {
				    return A->GetPersistentCacheHash() < B->GetPersistentCacheHash();
			    });
			return {};
		}
		default:
			return Invalid("Unknown pipeline kind.");
		}
	}
}
