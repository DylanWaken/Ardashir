/** @file ArdaRHICapabilities.h
 * Declares structured desktop-GPU capabilities and feature requirements.
 */

#pragma once

#include "ArdaRHITypes.h"

namespace arda
{
	inline constexpr uint32_t ArdaRHIInvalidQueueFamily = 0xffffffffu;

	/** Native ray-tracing implementation level. */
	enum class EArdaRHIRayTracingTier : uint8_t
	{
		/** No native or software ray-tracing implementation. */
		None,
		/** Software-emulated ray tracing. */
		Software,
		/** Hardware acceleration structures and shader traversal. */
		HardwareAccelerationStructures,
		/** Hardware traversal including inline ray queries. */
		HardwareInlineQueries,
		/** Hardware traversal including opacity micromaps. */
		HardwareOpacityMicromaps
	};

	/** Native mesh-shader implementation level. */
	enum class EArdaRHIMeshShaderTier : uint8_t
	{
		None,
		/** Mesh and optional amplification shader stages are available. */
		MeshAndAmplificationShaders
	};
	/** Native work-graph implementation level. */
	enum class EArdaRHIWorkGraphTier : uint8_t
	{
		None,
		/** Compute-style work-graph nodes are available. */
		ComputeNodes,
		/** Work graphs can feed mesh nodes into rasterization. */
		MeshNodes
	};
	/** Native sampler-feedback implementation level. */
	enum class EArdaRHISamplerFeedbackTier : uint8_t
	{
		None,
		/** Feedback is limited to wrap/clamp addressing and full-resource views. */
		RestrictedAddressingAndViews,
		/** Feedback supports every addressing mode and arbitrary resource views. */
		UnrestrictedAddressingAndViews
	};

	/** Ray-tracing abilities and native limits reported by one device. */
	struct FArdaRHIRayTracingCapabilities
	{
		/** Ray-tracing object and execution infrastructure exists; inspect individual operations before use. */
		bool mbInfrastructure = false;
		/** Traversal uses hardware acceleration rather than a software implementation. */
		bool mbHardwareAccelerated = false;
		/** Ray-generation/miss/hit/callable pipeline execution is supported. */
		bool mbPipelineShaders = false;
		/** Ordinary shaders may issue inline ray queries against built acceleration structures. */
		bool mbInlineRayQueries = false;
		/** Acceleration-structure allocation and build operations are admitted. */
		bool mbAccelerationStructures = false;
		/** Bottom-level geometry acceleration structures can be built. */
		bool mbBottomLevel = false;
		/** Top-level instance acceleration structures can be built. */
		bool mbTopLevel = false;
		/** A structure built with update permission can be updated within its declared constraints. */
		bool mbBuildUpdate = false;
		/** Compacted-size queries and acceleration-structure compaction are supported. */
		bool mbCompaction = false;
		/** Ray dispatch dimensions/records can be supplied through an indirect argument buffer. */
		bool mbIndirectDispatch = false;
		/** Top-level build inputs can use the supported GPU instance/indirect build path. */
		bool mbIndirectTopLevelBuild = false;
		/** Shader-table records may carry the supported local argument/binding payload. */
		bool mbLocalShaderTableArguments = false;
		/** Explicitly populated shader tables can be committed and retained across dispatches. */
		bool mbPersistentShaderTables = false;
		/** Opacity micromaps can be built and referenced by compatible ray-tracing geometry. */
		bool mbOpacityMicromaps = false;
		/** Native shader identifier size in bytes, used when constructing shader records. */
		uint32_t mShaderIdentifierSize = 0;
		/** Required byte alignment of individual shader-table records. */
		uint32_t mShaderRecordAlignment = 0;
		/** Required byte alignment of shader-table sections/base addresses. */
		uint32_t mShaderTableAlignment = 0;
		/** Required alignment of acceleration-structure storage, in bytes. */
		uint32_t mAccelerationStructureAlignment = 0;
		/** Maximum pipeline ray recursion depth admitted by this device. */
		uint32_t mMaxRecursionDepth = 0;
		/** Zero means the native API does not expose a queryable payload limit. */
		uint32_t mMaxRayPayloadSize = 0;
		/** Maximum ray-generation invocations accepted by one direct dispatch. */
		uint32_t mMaxRayDispatchInvocations = 0;

		/**
         * Derives the summary tier from the authoritative individual abilities.
         * @return The highest fully reported ray-tracing implementation level.
         */
		[[nodiscard]] EArdaRHIRayTracingTier GetTier() const noexcept
		{
			if (!mbInfrastructure)
			{
				return EArdaRHIRayTracingTier::None;
			}
			if (!mbHardwareAccelerated)
			{
				return EArdaRHIRayTracingTier::Software;
			}
			if (!mbAccelerationStructures)
			{
				return EArdaRHIRayTracingTier::None;
			}
			if (mbOpacityMicromaps)
			{
				return EArdaRHIRayTracingTier::HardwareOpacityMicromaps;
			}
			if (mbInlineRayQueries)
			{
				return EArdaRHIRayTracingTier::HardwareInlineQueries;
			}
			return EArdaRHIRayTracingTier::HardwareAccelerationStructures;
		}
	};

	/** Bindless and direct descriptor-indexing abilities. */
	struct FArdaRHIDescriptorCapabilities
	{
		/** Shader-indexed descriptor tables are supported; other descriptor flags refine their use. */
		bool mbBindless = false;
		/** Runtime-sized shader descriptor arrays are supported. */
		bool mbRuntimeDescriptorArrays = false;
		/** Unbounded descriptor declarations are admitted by the native binding model. */
		bool mbUnboundedArrays = false;
		/** Unused entries in a supported descriptor array may remain unpopulated. */
		bool mbPartiallyBound = false;
		/** Supported descriptor bindings may be updated after binding with provider-managed versions. */
		bool mbUpdateAfterBind = false;
		/** Unused descriptor entries can be updated while earlier work remains pending. */
		bool mbUpdateUnusedWhilePending = false;
		/** A supported descriptor binding can allocate a variable descriptor count. */
		bool mbVariableDescriptorCount = false;
		/** Shaders may directly index the supported resource descriptor heap model. */
		bool mbDirectResourceHeapIndexing = false;
		/** Shaders may directly index the supported sampler heap independently of resource heaps. */
		bool mbDirectSamplerHeapIndexing = false;
		/** The Vulkan descriptor-buffer binding path is available. */
		bool mbDescriptorBuffer = false;
		/** The native descriptor-heap model is available; inspect direct-indexing flags for shader access. */
		bool mbDescriptorHeap = false;
		/** Maximum resource descriptor count exposed by this binding implementation. */
		uint32_t mMaxResourceDescriptors = 0;
		/** Maximum sampler descriptor count exposed by this binding implementation. */
		uint32_t mMaxSamplerDescriptors = 0;
	};

	/** Queue topology and native GPU synchronization abilities. */
	struct FArdaRHIQueueCapabilities
	{
		/** A graphics queue can record and execute supported graphics work. */
		bool mbGraphics = true;
		/** A compute queue is available; this does not imply a dedicated native family. */
		bool mbCompute = false;
		/** A copy queue is available; this does not imply a dedicated native family. */
		bool mbCopy = false;
		/** Compute work has a native family independent of the graphics family. */
		bool mbDedicatedComputeFamily = false;
		/** Copy work has a native family independent of graphics/compute execution. */
		bool mbDedicatedCopyFamily = false;
		/** QueueWait can order a consumer queue after a producer submission without a CPU wait. */
		bool mbGpuWaits = false;
		/** Native submission progress supports ordered timeline-style synchronization. */
		bool mbTimelineSynchronization = false;
		/** The provider can encode required native queue-family resource ownership transfers. */
		bool mbQueueFamilyOwnershipTransfer = false;
		/** A queue capable of native sparse binding is available. */
		bool mbSparseBindingQueue = false;
		/** Native graphics queue-family identity, or ArdaRHIInvalidQueueFamily when unavailable. */
		uint32_t mGraphicsFamily = ArdaRHIInvalidQueueFamily;
		/** Native compute queue-family identity; may equal the graphics family. */
		uint32_t mComputeFamily = ArdaRHIInvalidQueueFamily;
		/** Native copy queue-family identity; may share another family. */
		uint32_t mCopyFamily = ArdaRHIInvalidQueueFamily;
		/** Meaningful timestamp bits on graphics; zero means timestamp queries are unsupported. */
		uint32_t mGraphicsTimestampValidBits = 0;
		/** Meaningful timestamp bits on compute; independent of graphics timestamp support. */
		uint32_t mComputeTimestampValidBits = 0;
		/** Meaningful timestamp bits on copy; copy queues may not support timestamp queries. */
		uint32_t mCopyTimestampValidBits = 0;

		[[nodiscard]] uint32_t GetTimestampValidBits(EArdaRHIQueueType Queue) const noexcept
		{
			if (!IsSupported(Queue))
			{
				return 0;
			}
			switch (Queue)
			{
			case EArdaRHIQueueType::Graphics:
				return mGraphicsTimestampValidBits;
			case EArdaRHIQueueType::Compute:
				return mComputeTimestampValidBits;
			case EArdaRHIQueueType::Copy:
				return mCopyTimestampValidBits;
			}
			return 0;
		}

		/** Tests whether this queue can be timed without moving its work to another queue. */
		[[nodiscard]] bool SupportsTimestamps(EArdaRHIQueueType Queue) const noexcept
		{
			return GetTimestampValidBits(Queue) != 0;
		}

		[[nodiscard]] bool IsSupported(EArdaRHIQueueType Queue) const noexcept
		{
			switch (Queue)
			{
			case EArdaRHIQueueType::Graphics:
				return mbGraphics;
			case EArdaRHIQueueType::Compute:
				return mbCompute;
			case EArdaRHIQueueType::Copy:
				return mbCopy;
			}
			return false;
		}

		[[nodiscard]] uint32_t GetFamily(EArdaRHIQueueType Queue) const noexcept
		{
			switch (Queue)
			{
			case EArdaRHIQueueType::Graphics:
				return mGraphicsFamily;
			case EArdaRHIQueueType::Compute:
				return mComputeFamily;
			case EArdaRHIQueueType::Copy:
				return mCopyFamily;
			}
			return ArdaRHIInvalidQueueFamily;
		}
	};

	/** Sparse/reserved-resource and streaming-budget abilities. */
	struct FArdaRHIResidencyCapabilities
	{
		/** Sparse physical memory mappings are supported for the qualified resource types. */
		bool mbSparseBinding = false;
		/** Reserved buffers can receive explicit physical tile mappings. */
		bool mbReservedBuffers = false;
		/** Reserved 2D textures can receive explicit physical tile mappings. */
		bool mbReservedTexture2D = false;
		/** Reserved 3D textures can receive explicit physical tile mappings. */
		bool mbReservedTexture3D = false;
		/** Multiple compatible virtual mappings can refer to shared physical tiles with explicit ordering. */
		bool mbAliasedMappings = false;
		/** Native streaming memory usage and budget can be queried. */
		bool mbStreamingBudget = false;
		/** The provider supports requesting a streaming-budget reservation. */
		bool mbBudgetReservation = false;
		/** Native sparse allocation tile size in bytes; texture tile geometry is queried separately. */
		uint64_t mTileSizeInBytes = 0;
	};

	/** Portable compute facts used to admit ML-oriented modules. */
	struct FArdaRHIMachineLearningCapabilities
	{
		/** Shaders can use the supported subgroup/wave operation set. */
		bool mbSubgroupOperations = false;
		/** Shader arithmetic on native 16-bit floating-point values. */
		bool mbNativeFloat16 = false;
		/** Packed signed/unsigned 8-bit dot product with a 32-bit accumulator. */
		bool mbNativeInt8 = false;
		/** The provider supports shader/native buffer addresses; not a general CUDA-runtime pointer promise. */
		bool mbBufferDeviceAddress = false;
		/** Smallest qualified native subgroup width, in shader lanes. */
		uint32_t mSubgroupMinSize = 0;
		/** Largest qualified native subgroup width, in shader lanes. */
		uint32_t mSubgroupMaxSize = 0;
	};

	/** A module's explicit desktop-GPU admission requirements. */
	struct FArdaRHIFeatureRequirements
	{
		/** When true, reject a device missing ray-tracing infrastructure. */
		bool mbRequireRayTracingInfrastructure = false;
		/** When true, reject a device missing hardware ray tracing. */
		bool mbRequireHardwareRayTracing = false;
		/** When true, reject a device missing ray-tracing pipelines. */
		bool mbRequireRayTracingPipelines = false;
		/** When true, reject a device missing acceleration structures. */
		bool mbRequireAccelerationStructures = false;
		/** When true, reject a device missing acceleration-structure update. */
		bool mbRequireAccelerationStructureUpdate = false;
		/** When true, reject a device missing acceleration-structure compaction. */
		bool mbRequireAccelerationStructureCompaction = false;
		/** When true, reject a device missing indirect ray dispatch. */
		bool mbRequireIndirectRayDispatch = false;
		/** When true, reject a device missing local shader-table arguments. */
		bool mbRequireLocalShaderTableArguments = false;
		/** When true, reject a device missing opacity micromaps. */
		bool mbRequireOpacityMicromaps = false;
		/** When true, reject a device missing mesh shaders. */
		bool mbRequireMeshShaders = false;
		/** When true, reject a device missing unbounded descriptors. */
		bool mbRequireUnboundedDescriptors = false;
		/** When true, reject a device missing descriptor update-after-bind. */
		bool mbRequireUpdateAfterBind = false;
		/** When true, reject a device missing direct descriptor indexing. */
		bool mbRequireDirectDescriptorIndexing = false;
		/** When true, reject a device missing dedicated compute queue. */
		bool mbRequireDedicatedComputeQueue = false;
		/** When true, reject a device missing dedicated copy queue. */
		bool mbRequireDedicatedCopyQueue = false;
		/** When true, reject a device missing GPU queue waits. */
		bool mbRequireGpuQueueWaits = false;
		/** When true, reject a device missing sparse residency. */
		bool mbRequireSparseResidency = false;
		/** When true, reject a device missing streaming budget telemetry. */
		bool mbRequireStreamingBudget = false;
		/** When true, reject a device missing native sampler feedback. */
		bool mbRequireSamplerFeedback = false;
		/** When true, reject a device missing work graphs. */
		bool mbRequireWorkGraphs = false;
		/** When true, reject a device missing shader bundles. */
		bool mbRequireShaderBundles = false;
		/** When true, reject a device missing custom present. */
		bool mbRequireCustomPresent = false;
		/** When true, reject a device missing native float16. */
		bool mbRequireNativeFloat16 = false;
		/** When true, reject a device missing native int8. */
		bool mbRequireNativeInt8 = false;
	};

	/** Detailed result of evaluating a feature requirement set. */
	struct FArdaRHIFeatureSupportReport
	{
		/** All requested abilities absent from this device; an empty list means admission succeeded. */
		eastl::vector<eastl::string> mMissingAbilities;

		[[nodiscard]] bool IsSupported() const noexcept
		{
			/** All requested abilities absent from this device; an empty list means admission succeeded. */
			return mMissingAbilities.empty();
		}

		[[nodiscard]] FArdaRHIStatus ToStatus() const
		{
			if (mMissingAbilities.empty())
			{
				return {};
			}
			eastl::string Message = "Missing RHI abilities: ";
			for (size_t Index = 0; Index < mMissingAbilities.size(); ++Index)
			{
				if (Index)
				{
					Message += ", ";
				}
				Message += mMissingAbilities[Index];
			}
			return {EArdaRHIResult::Unsupported, eastl::move(Message)};
		}
	};

	/** Describes all capabilities reported by an RHI device. */
	struct FArdaRHICapabilities
	{
		/** Individual ray operations and limits; summary tier is derived from these facts. */
		FArdaRHIRayTracingCapabilities mRayTracing;
		/** Descriptor-array, heap/indexing and capacity facts. */
		FArdaRHIDescriptorCapabilities mDescriptors;
		/** Queue availability, family topology and synchronization facts. */
		FArdaRHIQueueCapabilities mQueues;
		/** Reserved-resource mapping and streaming budget facts. */
		FArdaRHIResidencyCapabilities mResidency;
		/** Portable shader arithmetic and subgroup facts used for ML-oriented workloads. */
		FArdaRHIMachineLearningCapabilities mMachineLearning;
		/** Highest qualified mesh/amplification shader tier; None disables meshlet pipelines. */
		EArdaRHIMeshShaderTier mMeshShaderTier = EArdaRHIMeshShaderTier::None;
		/** Highest qualified native work-graph node tier; independent of shader bundles. */
		EArdaRHIWorkGraphTier mWorkGraphTier = EArdaRHIWorkGraphTier::None;
		/** Qualified native feedback addressing/view tier, or None. */
		EArdaRHISamplerFeedbackTier mSamplerFeedbackTier = EArdaRHISamplerFeedbackTier::None;
		/** The provider can execute the supported shader-bundle record families. */
		bool mbShaderBundleDispatch = false;
		/** The presentation path accepts a custom back-buffer consumer/present callback. */
		bool mbCustomPresent = false;
		/** Typed resource collections expose retained indexed descriptors to shaders. */
		bool mbResourceCollections = false;

		// Independent portable abilities not represented by a structured tier.
		/** Conservative raster pipeline behavior is implemented and qualified when true. */
		bool mbConservativeRasterization = false;
		/** Variable shading-rate behavior is implemented and qualified when true. */
		bool mbVariableRateShading = false;
		/** Resources can be created before explicit heap memory is bound. */
		bool mbVirtualResources = false;
		/** Explicit native heap creation and compatible resource placement are supported. */
		bool mbHeaps = false;
		/** Required capacity multiple for native explicit heaps, before resource-specific alignment. */
		uint64_t mHeapAllocationAlignment = 65536;
		/** CPU-visible pitched texture staging and map/unmap operations are supported. */
		bool mbStagingTextures = false;
		/** Qualified texture region/subresource copy operations are supported. */
		bool mbTextureCopies = false;
		/** Qualified multisample texture resolve operations are supported. */
		bool mbTextureResolve = false;
		/** Explicit portable resource transitions reach native barriers. */
		bool mbExplicitTransitions = false;
		/** Paired begin/end transitions are supported within the documented queue contract. */
		bool mbSplitTransitions = false;
		/** Supported draw/dispatch argument buffers can drive indirect execution. */
		bool mbIndirectCommands = false;
		/** Barriers can establish a new active resource in overlapping heap memory. */
		bool mbAliasingBarriers = false;
		/** Event, timer and GPU fence query paths are implemented. */
		bool mbQueries = false;
		/** Retained compiled libraries can provide entry-point shader objects. */
		bool mbShaderLibraries = false;
		/** Compatible native pipeline-cache data can be persisted between runs. */
		bool mbPipelineCachePersistence = false;

		[[nodiscard]] bool IsQueueSupported(EArdaRHIQueueType Queue) const noexcept
		{
			/** Queue availability, family topology and synchronization facts. */
			return mQueues.IsSupported(Queue);
		}

		/** Evaluates every requested ability and returns all failures. */
		[[nodiscard]] FArdaRHIFeatureSupportReport Evaluate(const FArdaRHIFeatureRequirements& R) const
		{
			FArdaRHIFeatureSupportReport Report;
			const auto Need = [&Report](bool Required, bool Present, const char* Name)
			{
				if (Required && !Present)
				{
					Report.mMissingAbilities.push_back(Name);
				}
			};
			Need(R.mbRequireRayTracingInfrastructure, mRayTracing.mbInfrastructure, "ray-tracing infrastructure");
			Need(R.mbRequireHardwareRayTracing, mRayTracing.mbHardwareAccelerated, "hardware ray tracing");
			Need(R.mbRequireRayTracingPipelines, mRayTracing.mbPipelineShaders, "ray-tracing pipelines");
			Need(R.mbRequireAccelerationStructures, mRayTracing.mbAccelerationStructures, "acceleration structures");
			Need(R.mbRequireAccelerationStructureUpdate, mRayTracing.mbBuildUpdate, "acceleration-structure update");
			Need(R.mbRequireAccelerationStructureCompaction,
			    mRayTracing.mbCompaction,
			    "acceleration-structure compaction");
			Need(R.mbRequireIndirectRayDispatch, mRayTracing.mbIndirectDispatch, "indirect ray dispatch");
			Need(R.mbRequireLocalShaderTableArguments,
			    mRayTracing.mbLocalShaderTableArguments,
			    "local shader-table arguments");
			Need(R.mbRequireOpacityMicromaps, mRayTracing.mbOpacityMicromaps, "opacity micromaps");
			Need(R.mbRequireMeshShaders, mMeshShaderTier != EArdaRHIMeshShaderTier::None, "mesh shaders");
			Need(R.mbRequireUnboundedDescriptors, mDescriptors.mbUnboundedArrays, "unbounded descriptors");
			Need(R.mbRequireUpdateAfterBind, mDescriptors.mbUpdateAfterBind, "descriptor update-after-bind");
			Need(R.mbRequireDirectDescriptorIndexing,
			    mDescriptors.mbDirectResourceHeapIndexing,
			    "direct descriptor indexing");
			Need(R.mbRequireDedicatedComputeQueue, mQueues.mbDedicatedComputeFamily, "dedicated compute queue");
			Need(R.mbRequireDedicatedCopyQueue, mQueues.mbDedicatedCopyFamily, "dedicated copy queue");
			Need(R.mbRequireGpuQueueWaits, mQueues.mbGpuWaits, "GPU queue waits");
			Need(R.mbRequireSparseResidency, mResidency.mbSparseBinding, "sparse residency");
			Need(R.mbRequireStreamingBudget, mResidency.mbStreamingBudget, "streaming budget telemetry");
			Need(R.mbRequireSamplerFeedback,
			    mSamplerFeedbackTier != EArdaRHISamplerFeedbackTier::None,
			    "native sampler feedback");
			Need(R.mbRequireWorkGraphs, mWorkGraphTier != EArdaRHIWorkGraphTier::None, "work graphs");
			Need(R.mbRequireShaderBundles, mbShaderBundleDispatch, "shader bundles");
			Need(R.mbRequireCustomPresent, mbCustomPresent, "custom present");
			Need(R.mbRequireNativeFloat16, mMachineLearning.mbNativeFloat16, "native float16");
			Need(R.mbRequireNativeInt8, mMachineLearning.mbNativeInt8, "native int8");
			return Report;
		}
	};
}
