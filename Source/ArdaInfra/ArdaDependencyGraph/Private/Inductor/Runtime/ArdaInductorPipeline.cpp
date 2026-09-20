#include "Inductor/Rules/ArdaInductorPipelineInternal.h"

namespace arda
{
	using namespace pipeline_detail;

	TArdaRHIResult<FArdaInductorResolvedPipeline> ResolveArdaInductorPipeline(FArdaPipelineStateCache& Cache,
	    const FArdaInductorPipelinePattern& Pattern,
	    const FArdaRHIFramebufferRef& Framebuffer)
	{
		const auto Key = ConfigurationKey(Pattern.mConfiguration);
		if (!Key.mbValid || !Pattern.mStableKey || Pattern.mStableKey != Key.Finish())
		{
			return {{}, Invalid("Pipeline pattern was modified after inference or has no stable identity.")};
		}
		FArdaInductorResolvedPipeline Result;
		Result.mKind = Pattern.mConfiguration.mKind;
		Result.mStableKey = Pattern.mStableKey;
		FArdaRHIStatus Status;
		const auto& C = Pattern.mConfiguration;
		switch (Result.mKind)
		{
		case EArdaPipelineStateKind::Compute:
			Status = Cache.GetOrCreateCompute(C.mCompute, Result.mCompute);
			break;
		case EArdaPipelineStateKind::Graphics:
			Status = Cache.GetOrCreateGraphics(C.mGraphics, Framebuffer, Result.mGraphics);
			break;
		case EArdaPipelineStateKind::Meshlet:
			Status = Cache.GetOrCreateMeshlet(C.mMeshlet, Framebuffer, Result.mMeshlet);
			break;
		case EArdaPipelineStateKind::RayTracing:
			Status = Cache.GetOrCreateRayTracing(C.mRayTracing, Result.mRayTracing);
			break;
		case EArdaPipelineStateKind::WorkGraph:
			Status = Cache.GetOrCreateWorkGraph(C.mWorkGraph, Result.mWorkGraph);
			break;
		default:
			return {{}, Invalid("Unknown pipeline kind.")};
		}
		if (!Status)
		{
			return {{}, Status};
		}
		auto Concrete = C;
		switch (Result.mKind)
		{
		case EArdaPipelineStateKind::Compute:
			Concrete.mCompute.mDesc = Result.mCompute->GetDesc();
			break;
		case EArdaPipelineStateKind::Graphics:
			Concrete.mGraphics.mDesc = Result.mGraphics->GetDesc();
			break;
		case EArdaPipelineStateKind::Meshlet:
			Concrete.mMeshlet.mDesc = Result.mMeshlet->GetDesc();
			break;
		case EArdaPipelineStateKind::RayTracing:
			Concrete.mRayTracing.mDesc = Result.mRayTracing->GetDesc();
			break;
		case EArdaPipelineStateKind::WorkGraph:
			Concrete.mWorkGraph.mDesc = Result.mWorkGraph->GetDesc();
			break;
		}
		Result.mStableKey = ConfigurationKey(Concrete).Finish();
		return {eastl::move(Result), {}};
	}
}
