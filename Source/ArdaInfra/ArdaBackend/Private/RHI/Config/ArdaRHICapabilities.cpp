#include "RHI/Config/ArdaRHICapabilities.h"

namespace arda
{
	void FArdaRHICapabilities::AddPortableFeatures(FArdaBackendFeatureList& Features) const
	{
		// The initialized queue, command implementation and enabled device features qualify shared operations.
		const bool KnownSubgroupWidth = mMachineLearning.mbSubgroupOperations &&
		    mMachineLearning.mSubgroupMinSize != 0 &&
		    mMachineLearning.mSubgroupMaxSize >= mMachineLearning.mSubgroupMinSize;

		Features.Add("buffer.max-bytes", mLimits.mMaxBufferSize);
		Features.Add("buffer.max-storage-range", mLimits.mMaxStorageBufferRange);
		Features.Add("buffer.max-uniform-range", mLimits.mMaxUniformBufferRange);
		Features.Add("buffer.storage-offset-alignment", mLimits.mMinStorageBufferOffsetAlignment);
		Features.Add("buffer.uniform-offset-alignment", mLimits.mMinUniformBufferOffsetAlignment);
		Features.Add("compute.max-workgroup-count-x", mLimits.mMaxComputeWorkGroupCount[0]);
		Features.Add("compute.max-workgroup-count-y", mLimits.mMaxComputeWorkGroupCount[1]);
		Features.Add("compute.max-workgroup-count-z", mLimits.mMaxComputeWorkGroupCount[2]);
		Features.Add("compute.max-workgroup-invocations", mLimits.mMaxComputeWorkGroupInvocations);
		Features.Add("compute.max-workgroup-size-x", mLimits.mMaxComputeWorkGroupSize[0]);
		Features.Add("compute.max-workgroup-size-y", mLimits.mMaxComputeWorkGroupSize[1]);
		Features.Add("compute.max-workgroup-size-z", mLimits.mMaxComputeWorkGroupSize[2]);
		Features.Add("rendering.max-color-attachments", mLimits.mMaxColorAttachments);
		Features.Add("rendering.max-viewport-height", mLimits.mMaxViewportDimensions[1]);
		Features.Add("rendering.max-viewport-width", mLimits.mMaxViewportDimensions[0]);
		Features.Add("rendering.max-viewports", mLimits.mMaxViewports);
		Features.Add("texture.max-1d-dimension", mLimits.mMaxTexture1D);
		Features.Add("texture.max-2d-dimension", mLimits.mMaxTexture2D);
		Features.Add("texture.max-3d-dimension", mLimits.mMaxTexture3D);
		Features.Add("texture.max-array-layers", mLimits.mMaxTextureArrayLayers);
		Features.Add("texture.max-cube-dimension", mLimits.mMaxTextureCube);
		Features.Add("vertex.max-attributes", mLimits.mMaxVertexAttributes);
		Features.Add("vertex.max-bindings", mLimits.mMaxVertexBindings);
		Features.Add("vertex.max-stride", mLimits.mMaxVertexStride);

		Features.Add("command.indirect", mbIndirectCommands);
		Features.Add("command.indirect-first-instance", mbIndirectFirstInstance);

		Features.Add("descriptor.bindless", mDescriptors.mbBindless);
		Features.Add("descriptor.buffer", mDescriptors.mbDescriptorBuffer);
		Features.Add("descriptor.direct-resource-heap-indexing", mDescriptors.mbDirectResourceHeapIndexing);
		Features.Add("descriptor.direct-sampler-heap-indexing", mDescriptors.mbDirectSamplerHeapIndexing);
		Features.Add("descriptor.heap", mDescriptors.mbDescriptorHeap);
		Features.Add("descriptor.partially-bound", mDescriptors.mbPartiallyBound);
		Features.Add("descriptor.resource-capacity", mDescriptors.mMaxResourceDescriptors);
		Features.Add("descriptor.runtime-arrays", mDescriptors.mbRuntimeDescriptorArrays);
		Features.Add("descriptor.sampler-capacity", mDescriptors.mMaxSamplerDescriptors);
		Features.Add("descriptor.unbounded-arrays", mDescriptors.mbUnboundedArrays);
		Features.Add("descriptor.update-after-bind", mDescriptors.mbUpdateAfterBind);
		Features.Add("descriptor.update-unused-while-pending", mDescriptors.mbUpdateUnusedWhilePending);
		Features.Add("descriptor.variable-count", mDescriptors.mbVariableDescriptorCount);

		Features.Add("format.support-reported", mbFormatSupportReported);
		Features.Add("memory.budget-reservation", mResidency.mbBudgetReservation);
		Features.Add("memory.heap-capacity-alignment", mHeapAllocationAlignment);
		Features.Add("memory.explicit-heap", mbHeaps);
		Features.Add("memory.streaming-budget", mResidency.mbStreamingBudget);

		Features.Add("pipeline.cache-persistence", mbPipelineCachePersistence);

		Features.Add("presentation.custom-present", mbCustomPresent);

		Features.Add("query.gpu", mbQueries);

		Features.Add("queue.compute", mQueues.mbCompute);
		Features.Add("queue.compute.timestamp-bits", mQueues.GetTimestampValidBits(EArdaRHIQueueType::Compute));
		Features.Add("queue.copy", mQueues.mbCopy);
		Features.Add("queue.copy.timestamp-bits", mQueues.GetTimestampValidBits(EArdaRHIQueueType::Copy));
		Features.Add("queue.dedicated-compute", mQueues.mbDedicatedComputeFamily);
		Features.Add("queue.dedicated-copy", mQueues.mbDedicatedCopyFamily);
		Features.Add("queue.family-ownership-transfer", mQueues.mbQueueFamilyOwnershipTransfer);
		Features.Add("queue.gpu-waits", mQueues.mbGpuWaits);
		Features.Add("queue.graphics", mQueues.mbGraphics);
		Features.Add("queue.graphics.timestamp-bits", mQueues.GetTimestampValidBits(EArdaRHIQueueType::Graphics));
		Features.Add("queue.sparse-binding", mQueues.mbSparseBindingQueue);

		Features.Add("ray-tracing.acceleration-structure", mRayTracing.mbAccelerationStructures);
		Features.Add("ray-tracing.acceleration-structure-alignment", mRayTracing.mAccelerationStructureAlignment);
		Features.Add("ray-tracing.acceleration-structure-compaction", mRayTracing.mbCompaction);
		Features.Add("ray-tracing.acceleration-structure-update", mRayTracing.mbBuildUpdate);
		Features.Add("ray-tracing.bottom-level-acceleration-structure", mRayTracing.mbBottomLevel);
		Features.Add("ray-tracing.hardware", mRayTracing.mbHardwareAccelerated);
		Features.Add("ray-tracing.indirect-dispatch", mRayTracing.mbIndirectDispatch);
		Features.Add("ray-tracing.indirect-top-level-build", mRayTracing.mbIndirectTopLevelBuild);
		Features.Add("ray-tracing.infrastructure", mRayTracing.mbInfrastructure);
		Features.Add("ray-tracing.inline-query", mRayTracing.mbInlineRayQueries);
		Features.Add("ray-tracing.local-shader-table-arguments", mRayTracing.mbLocalShaderTableArguments);
		Features.Add("ray-tracing.max-dispatch-invocations", mRayTracing.mMaxRayDispatchInvocations);
		Features.Add("ray-tracing.max-payload-bytes", mRayTracing.mMaxRayPayloadSize);
		Features.Add("ray-tracing.opacity-micromap", mRayTracing.mbOpacityMicromaps);
		Features.Add("ray-tracing.persistent-shader-tables", mRayTracing.mbPersistentShaderTables);
		Features.Add("ray-tracing.pipeline", mRayTracing.mbPipelineShaders);
		Features.Add("ray-tracing.recursion-depth", mRayTracing.mMaxRecursionDepth);
		Features.Add("ray-tracing.shader-identifier-bytes", mRayTracing.mShaderIdentifierSize);
		Features.Add("ray-tracing.shader-record-alignment", mRayTracing.mShaderRecordAlignment);
		Features.Add("ray-tracing.shader-table-alignment", mRayTracing.mShaderTableAlignment);
		Features.Add("ray-tracing.tier", static_cast<uint64_t>(mRayTracing.GetTier()));
		Features.Add("ray-tracing.top-level-acceleration-structure", mRayTracing.mbTopLevel);

		Features.Add("rendering.conservative-rasterization", mbConservativeRasterization);
		Features.Add("rendering.variable-rate-shading", mbVariableRateShading);

		Features.Add("resource.aliased-sparse-mapping", mResidency.mbAliasedMappings);
		Features.Add("resource.buffer-device-address", mMachineLearning.mbBufferDeviceAddress);
		Features.Add("resource.collections", mbResourceCollections);
		Features.Add("resource.reserved-buffer", mResidency.mbReservedBuffers);
		Features.Add("resource.reserved-texture-2d", mResidency.mbReservedTexture2D);
		Features.Add("resource.reserved-texture-3d", mResidency.mbReservedTexture3D);
		Features.Add("resource.sparse-tile-bytes", mResidency.mTileSizeInBytes);
		Features.Add("resource.sparse-binding", mResidency.mbSparseBinding);
		Features.Add("resource.virtual", mbVirtualResources);

		Features.Add("sampler.feedback", mSamplerFeedbackTier != EArdaRHISamplerFeedbackTier::None);
		Features.Add("sampler.feedback-tier", static_cast<uint64_t>(mSamplerFeedbackTier));

		Features.Add("shader.bundle-dispatch", mbShaderBundleDispatch);
		Features.Add("shader.float16", mMachineLearning.mbNativeFloat16);
		Features.Add("shader.fragment-stores-and-atomics", mbFragmentShaderStoresAndAtomics);
		Features.Add("shader.geometry", mbGeometryShaders);
		Features.Add("shader.integer-dot-product", mMachineLearning.mbNativeInt8);
		Features.Add("shader.library", mbShaderLibraries);
		Features.Add("shader.mesh", SupportsMeshShaderTier(EArdaRHIMeshShaderTier::MeshShadersOnly));
		Features.Add("shader.mesh-tier",
		    SupportsMeshShaderTier(EArdaRHIMeshShaderTier::MeshAndAmplificationShaders) ? 2
		        : SupportsMeshShaderTier(EArdaRHIMeshShaderTier::MeshShadersOnly)       ? 1
		                                                                                : 0);
		Features.Add("shader.subgroup-max-size", KnownSubgroupWidth ? mMachineLearning.mSubgroupMaxSize : 0);
		Features.Add("shader.subgroup-min-size", KnownSubgroupWidth ? mMachineLearning.mSubgroupMinSize : 0);
		Features.Add("shader.task", SupportsMeshShaderTier(EArdaRHIMeshShaderTier::MeshAndAmplificationShaders));
		Features.Add("shader.tessellation", mbTessellationShaders);
		Features.Add("shader.vertex-stores-and-atomics", mbVertexShaderStoresAndAtomics);
		Features.Add("shader.wave-operations", mMachineLearning.mbSubgroupOperations);

		Features.Add("synchronization.aliasing-barrier", mbAliasingBarriers);
		Features.Add("synchronization.explicit-transition", mbExplicitTransitions);
		Features.Add("synchronization.split-transition", mbSplitTransitions);
		Features.Add("synchronization.timeline-semaphore", mQueues.mbTimelineSynchronization);

		Features.Add("texture.copy", mbTextureCopies);
		Features.Add("texture.resolve", mbTextureResolve);
		Features.Add("texture.staging", mbStagingTextures);

		Features.Add("work-graph.dispatch", mWorkGraphTier != EArdaRHIWorkGraphTier::None);
		Features.Add("work-graph.tier", static_cast<uint64_t>(mWorkGraphTier));
	}

	void FArdaRHICapabilities::PublishBackendFeatures(eastl::shared_ptr<FArdaBackendFeatureList> Features)
	{
		if (!Features)
		{
			Features = eastl::make_shared<FArdaBackendFeatureList>();
		}
		else if (Features.use_count() > 1)
		{
			// A provider retaining its mutable builder cannot change an already published device snapshot.
			Features = eastl::make_shared<FArdaBackendFeatureList>(*Features);
		}
		AddPortableFeatures(*Features);
		mBackendFeatures = eastl::move(Features);
	}

	FArdaBackendFeatureListRef FArdaRHICapabilities::GetBackendFeatureList() const
	{
		if (mBackendFeatures)
		{
			return mBackendFeatures;
		}
		auto Features = eastl::make_shared<FArdaBackendFeatureList>();
		AddPortableFeatures(*Features);
		return Features;
	}

	FArdaRHIFeatureSupportReport FArdaRHICapabilities::Evaluate(const FArdaRHIFeatureRequirements& Requirements) const
	{
		if (Requirements.GetEntries().empty())
		{
			return {};
		}
		return GetBackendFeatureList()->Evaluate(Requirements);
	}

	FArdaRHIFeatureSupportReport FArdaBackendFeatureList::Evaluate(
	    const FArdaRHIFeatureRequirements& Requirements) const
	{
		FArdaRHIFeatureSupportReport Report;
		for (const auto& Required : Requirements.GetEntries())
		{
			if (!Required.IsValid() ||
			    !Supports(Required.mName.c_str(),
			        Required.mMinimumValue,
			        Required.mRequiredBits,
			        Required.mMaximumValue))
			{
				Report.mMissingAbilities.push_back(Required.mName);
			}
		}
		return Report;
	}
}
