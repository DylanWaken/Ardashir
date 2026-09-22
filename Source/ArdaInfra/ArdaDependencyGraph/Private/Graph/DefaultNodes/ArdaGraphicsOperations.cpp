#include "Graph/DefaultNodes/ArdaGraphicsOperations.h"
#include <cmath>

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaRasterizeNode, Rasterize);

	FArdaDependencyNodeMetadata FArdaRasterizeNode::GetMetadata()
	{
		return {"graphics.rasterize", 2};
	}

	eastl::string FArdaRasterizeNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder K;
		K.Value(P.mConfig.mViewport.mMinX)
		    .Value(P.mConfig.mViewport.mMaxX)
		    .Value(P.mConfig.mViewport.mMinY)
		    .Value(P.mConfig.mViewport.mMaxY)
		    .Value(P.mConfig.mViewport.mMinZ)
		    .Value(P.mConfig.mViewport.mMaxZ)
		    .Value(P.mConfig.mDraw.mVertexCount)
		    .Value(P.mConfig.mDraw.mInstanceCount)
		    .Value(P.mConfig.mDraw.mStartIndex)
		    .Value(P.mConfig.mDraw.mStartVertex)
		    .Value(P.mConfig.mDraw.mStartInstance)
		    .Value(P.mConfig.mScissor.mMinX)
		    .Value(P.mConfig.mScissor.mMaxX)
		    .Value(P.mConfig.mScissor.mMinY)
		    .Value(P.mConfig.mScissor.mMaxY)
		    .Value(P.mConfig.mRasterState.mFillMode)
		    .Value(P.mConfig.mRasterState.mCullMode)
		    .Value(P.mConfig.mRasterState.mbFrontCounterClockwise)
		    .Value(P.mConfig.mRasterState.mbDepthClip)
		    .Value(P.mConfig.mRasterState.mbScissor)
		    .Value(P.mConfig.mDepthStencilState.mbDepthTest)
		    .Value(P.mConfig.mDepthStencilState.mbDepthWrite)
		    .Value(P.mConfig.mDepthStencilState.mDepthFunc)
		    .Value(P.mConfig.mBlendState.mbAlphaToCoverage)
		    .Value(P.mConfig.mTopology)
		    .Value(P.mConfig.mPatchControlPoints);
		for (const auto& T : P.mConfig.mBlendState.mTargets)
		{
			K.Value(T.mbEnable)
			    .Value(T.mSourceColor)
			    .Value(T.mDestinationColor)
			    .Value(T.mSourceAlpha)
			    .Value(T.mDestinationAlpha);
		}
		return K.Build();
	}

	FArdaRHIStatus FArdaRasterizeNode::Validate(const FArdaParameters& P)
	{
		const auto& V = P.mConfig.mViewport;
		if (!std::isfinite(V.mMinX) || !std::isfinite(V.mMaxX) || !std::isfinite(V.mMinY) || !std::isfinite(V.mMaxY) ||
		    !std::isfinite(V.mMinZ) || !std::isfinite(V.mMaxZ) || V.mMaxX <= V.mMinX || V.mMaxY <= V.mMinY ||
		    V.mMinZ < 0.f || V.mMaxZ > 1.f || V.mMaxZ < V.mMinZ || V.mMinX < float(INT32_MIN) ||
		    double(V.mMaxX) > double(INT32_MAX) || V.mMinY < float(INT32_MIN) || double(V.mMaxY) > double(INT32_MAX))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Rasterization requires a finite nonempty viewport.");
		}
		if (P.mConfig.mRasterState.mbScissor &&
		    (P.mConfig.mScissor.mMaxX <= P.mConfig.mScissor.mMinX ||
		        P.mConfig.mScissor.mMaxY <= P.mConfig.mScissor.mMinY))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Enabled raster scissor must be nonempty.");
		}
		if (P.mConfig.mTopology > EArdaRHIPrimitiveTopology::PatchList || !P.mConfig.mDraw.mInstanceCount ||
		    (P.mConfig.mTopology == EArdaRHIPrimitiveTopology::PatchList
		            ? !P.mConfig.mPatchControlPoints || P.mConfig.mPatchControlPoints > 32
		            : P.mConfig.mPatchControlPoints != 0))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Rasterization requires a supported primitive topology and instance count.");
		}
		return {};
	}

	FArdaDependencyNodeDesc FArdaRasterizeNode::Describe(const FArdaParameters& P, const FArdaState&)
	{
		auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		auto& D = Config->mGraphics.mDesc;
		D.mRasterState = P.mConfig.mRasterState;
		D.mDepthStencilState = P.mConfig.mDepthStencilState;
		D.mBlendState = P.mConfig.mBlendState;
		D.mTopology = P.mConfig.mTopology;
		D.mPatchControlPoints = P.mConfig.mPatchControlPoints;
		FArdaDependencyNodeDesc Result;
		Result.mbAutomaticRasterFamily = true;
		FArdaInductorPipelineContribution C;
		C.mConfiguration = eastl::move(Config);
		Result.mPipelineStages.push_back(eastl::move(C));
		return Result;
	}

	FArdaRHIStatus FArdaRasterizeNode::ConfigureGraphics(FArdaDependencyExecutionContext&,
	    const FArdaParameters& P,
	    const FArdaState&,
	    FArdaInstanceState&,
	    FArdaGraphicsWork& Work)
	{
		Work.mState.mViewports = {P.mConfig.mViewport};
		Work.mState.mScissors = {P.mConfig.mRasterState.mbScissor
		        ? P.mConfig.mScissor
		        : FArdaRHIRect{int32_t(std::floor(P.mConfig.mViewport.mMinX)),
		              int32_t(std::ceil(P.mConfig.mViewport.mMaxX)),
		              int32_t(std::floor(P.mConfig.mViewport.mMinY)),
		              int32_t(std::ceil(P.mConfig.mViewport.mMaxY))}};
		if (P.mConfig.mDraw.mVertexCount)
		{
			Work.mDraw = P.mConfig.mDraw;
		}
		Work.mMeshletState.mViewports = Work.mState.mViewports;
		Work.mMeshletState.mScissors = Work.mState.mScissors;
		return {};
	}

	FArdaGraphBuffer FetchBuffer(FArdaGraphEdit& Edit,
	    FArdaGraphStageValue Value,
	    const FArdaStageBufferRequest& Request)
	{
		return Edit.FetchBuffer(Value, Request);
	}
}
