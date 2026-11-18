/** Field order shared by graph archives and construction keys. Changing it requires an archive version decision. */
#pragma once

namespace arda
{
	template <class FieldSink, class Descriptor>
	void BufferDescriptorFields(FieldSink&& Sink, Descriptor& Value)
	{
		Sink.Fields(Value.mByteSize,
		    Value.mStructureStride,
		    Value.mMaxVersions,
		    Value.mFormat,
		    Value.mUsage,
		    Value.mCpuAccess,
		    Value.mInitialState,
		    Value.mbKeepInitialState,
		    Value.mbVirtual,
		    Value.mbTiled,
		    Value.mDebugName,
		    Value.mbCudaInterop);
	}

	template <class FieldSink, class Descriptor>
	void TextureDescriptorFields(FieldSink&& Sink, Descriptor& Value)
	{
		Sink.Fields(Value.mWidth,
		    Value.mHeight,
		    Value.mDepth,
		    Value.mArraySize,
		    Value.mMipLevels,
		    Value.mSampleCount,
		    Value.mFormat,
		    Value.mDimension,
		    Value.mUsage,
		    Value.mInitialState,
		    Value.mbKeepInitialState,
		    Value.mbVirtual,
		    Value.mbTiled,
		    Value.mClearValue.mR,
		    Value.mClearValue.mG,
		    Value.mClearValue.mB,
		    Value.mClearValue.mA,
		    Value.mbUseClearValue,
		    Value.mDebugName,
		    Value.mbCudaInterop);
	}

	template <class FieldSink, class Descriptor>
	void InductorOptionFields(FieldSink&& Sink, Descriptor& Value)
	{
		Sink.Fields(Value.mObjective,
		    Value.mSearchMode,
		    Value.mMaxVramBytes,
		    Value.mMinimumAsyncChain,
		    Value.mMinimumAsyncSlack,
		    Value.mMaxSearchStates,
		    Value.mFramesInFlight,
		    Value.mCudaGraphMode,
		    Value.mCudaHandoffCost,
		    Value.mAliasSubmissionCost,
		    Value.mbEnableGpuTiming,
		    Value.mGpuTimingEmaAlpha,
		    Value.mGpuTimingSampleInterval,
		    Value.mGpuTimingHistoryCapacity,
		    Value.mbEnableAdaptiveScheduling,
		    Value.mAdaptiveSchedulingInterval,
		    Value.mAdaptiveSchedulingMinSamples,
		    Value.mAdaptiveSchedulingSearchBudget,
		    Value.mAdaptiveSchedulingMinImprovement,
		    Value.mbEnableCopyQueue,
		    Value.mbEnableAsyncCompute);
	}
}
