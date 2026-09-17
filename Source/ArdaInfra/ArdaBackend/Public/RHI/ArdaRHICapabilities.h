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
		/** Both mesh and amplification shader stages are available. */
		MeshAndAmplificationShaders,
		/** Mesh shaders are available without amplification shaders; numeric values are not tier ordering. */
		MeshShadersOnly
	};

	/** Immutable device limits. Zero means unreported, rather than an unlimited or supported operation. */
	struct FArdaRHIDeviceLimits
	{
		/** Maximum width of a one-dimensional texture. */
		uint32_t mMaxTexture1D = 0;
		/** Maximum width or height of a two-dimensional texture. */
		uint32_t mMaxTexture2D = 0;
		/** Maximum width, height, or depth of a volume texture. */
		uint32_t mMaxTexture3D = 0;
		/** Maximum width or height of a cube face. */
		uint32_t mMaxTextureCube = 0;
		/** Maximum array layers, counting each cube face as one layer. */
		uint32_t mMaxTextureArrayLayers = 0;
		/** Maximum simultaneous color attachments. */
		uint32_t mMaxColorAttachments = 0;
		/** Maximum simultaneous viewports and scissors. */
		uint32_t mMaxViewports = 0;
		/** Maximum viewport width and height; zero means unreported. */
		uint32_t mMaxViewportDimensions[2]{};
		/** Minimum and maximum viewport coordinates, including the far edge; a zero pair is unreported. */
		float mViewportBounds[2]{};
		/** Maximum vertex attributes in one input layout. */
		uint32_t mMaxVertexAttributes = 0;
		/** Maximum vertex buffer binding slots. */
		uint32_t mMaxVertexBindings = 0;
		/** Maximum vertex element stride in bytes. */
		uint32_t mMaxVertexStride = 0;
		/** Maximum dispatched work-group count on the X, Y, and Z axes. */
		uint32_t mMaxComputeWorkGroupCount[3]{};
		/** Maximum shader work-group size on the X, Y, and Z axes. */
		uint32_t mMaxComputeWorkGroupSize[3]{};
		/** Maximum invocations in one shader work group. */
		uint32_t mMaxComputeWorkGroupInvocations = 0;
		/** Maximum native buffer allocation size in bytes. */
		uint64_t mMaxBufferSize = 0;
		/** Maximum bytes addressed by one uniform-buffer binding. */
		uint64_t mMaxUniformBufferRange = 0;
		/** Maximum bytes addressed by one storage-buffer binding. */
		uint64_t mMaxStorageBufferRange = 0;
		/** Required uniform-buffer binding offset alignment in bytes. */
		uint64_t mMinUniformBufferOffsetAlignment = 0;
		/** Required storage-buffer binding offset alignment in bytes. */
		uint64_t mMinStorageBufferOffsetAlignment = 0;
	};

	/** Device support for a typed format. Facts are independent; combined shape/usage admission remains native. */
	struct FArdaRHIFormatSupport
	{
		/** Native API format identifier, or zero when the format has no supported native mapping. */
		uint64_t mNativeFormat = 0;
		/** One-dimensional textures can use this format. */
		bool mbTexture1D = false;
		/** Two-dimensional textures can use this format. */
		bool mbTexture2D = false;
		/** Volume textures can use this format. */
		bool mbTexture3D = false;
		/** Cube textures can use this format. */
		bool mbTextureCube = false;
		/** Texture shader-resource views and shader reads are supported. */
		bool mbShaderResource = false;
		/** Sampled textures support linear filtering. */
		bool mbFilterable = false;
		/** Typed storage-image/unordered-access texture views are supported. */
		bool mbStorage = false;
		/** Typed storage texture loads are supported. */
		bool mbStorageLoad = false;
		/** Typed storage texture stores are supported. */
		bool mbStorageStore = false;
		/** Color render-target attachments are supported. */
		bool mbColorAttachment = false;
		/** Depth/stencil attachments are supported. */
		bool mbDepthStencilAttachment = false;
		/** Color attachment blending is supported. */
		bool mbBlendable = false;
		/** Vertex input attributes are supported. */
		bool mbVertexBuffer = false;
		/** Typed buffer shader-resource views are supported. */
		bool mbBufferShaderResource = false;
		/** Typed buffer unordered-access/storage views are supported. */
		bool mbBufferStorage = false;
		/** Supported texture sample counts: each count is its own bit (1 | 2 | 4 ...); zero means none. */
		uint32_t mSampleCounts = 0;
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
		/** When true, reject a device missing the graphics geometry shader stage. */
		bool mbRequireGeometryShaders = false;
		/** When true, reject a device missing graphics hull/domain tessellation stages. */
		bool mbRequireTessellationShaders = false;
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

		// Additional independent provider abilities. Existing flags retain their original meaning.
		/** Ordinary shaders must support inline ray queries. */
		bool mbRequireInlineRayQueries = false;
		/** Bottom-level geometry acceleration structures must be supported. */
		bool mbRequireBottomLevelAccelerationStructures = false;
		/** Top-level instance acceleration structures must be supported. */
		bool mbRequireTopLevelAccelerationStructures = false;
		/** GPU instance/indirect top-level build inputs must be supported. */
		bool mbRequireIndirectTopLevelBuild = false;
		/** Explicit shader tables must support commit and reuse across dispatches. */
		bool mbRequirePersistentShaderTables = false;
		/** Shader-indexed descriptor tables must be supported. */
		bool mbRequireBindless = false;
		/** Runtime-sized shader descriptor arrays must be supported. */
		bool mbRequireRuntimeDescriptorArrays = false;
		/** Unused descriptor-array entries must be allowed to remain unpopulated. */
		bool mbRequirePartiallyBoundDescriptors = false;
		/** Unused descriptor entries must support updates while earlier work remains pending. */
		bool mbRequireUpdateUnusedWhilePending = false;
		/** Variable descriptor-count allocation must be supported. */
		bool mbRequireVariableDescriptorCount = false;
		/** Shaders must support direct sampler-heap indexing; independent of resource heaps. */
		bool mbRequireDirectSamplerHeapIndexing = false;
		/** The native descriptor-buffer binding path must be supported. */
		bool mbRequireDescriptorBuffer = false;
		/** The native descriptor-heap binding model must be supported. */
		bool mbRequireDescriptorHeap = false;
		/** A graphics queue must be available. */
		bool mbRequireGraphicsQueue = false;
		/** A compute queue must be available, without requiring a dedicated native family. */
		bool mbRequireComputeQueue = false;
		/** A copy queue must be available, without requiring a dedicated native family. */
		bool mbRequireCopyQueue = false;
		/** Native timeline-style submission synchronization must be supported. */
		bool mbRequireTimelineSynchronization = false;
		/** The provider must support queue-family resource ownership transfers. */
		bool mbRequireQueueFamilyOwnershipTransfer = false;
		/** A queue capable of native sparse binding must be available. */
		bool mbRequireSparseBindingQueue = false;
		/** Reserved buffers must support explicit physical tile mappings. */
		bool mbRequireReservedBuffers = false;
		/** Reserved 2D textures must support explicit physical tile mappings. */
		bool mbRequireReservedTexture2D = false;
		/** Reserved 3D textures must support explicit physical tile mappings. */
		bool mbRequireReservedTexture3D = false;
		/** Compatible sparse mappings must support sharing physical tiles. */
		bool mbRequireAliasedMappings = false;
		/** The provider must support requesting a streaming-budget reservation. */
		bool mbRequireBudgetReservation = false;
		/** Typed retained resource collections must be supported. */
		bool mbRequireResourceCollections = false;
		/** Conservative rasterization must be supported. */
		bool mbRequireConservativeRasterization = false;
		/** Variable-rate shading must be supported. */
		bool mbRequireVariableRateShading = false;
		/** Resources must support creation before explicit heap memory is bound. */
		bool mbRequireVirtualResources = false;
		/** Explicit native heaps and compatible resource placement must be supported. */
		bool mbRequireHeaps = false;
		/** CPU-visible pitched staging textures must support map/unmap. */
		bool mbRequireStagingTextures = false;
		/** Qualified texture region/subresource copy operations must be supported. */
		bool mbRequireTextureCopies = false;
		/** Multisample texture resolve operations must be supported. */
		bool mbRequireTextureResolve = false;
		/** Explicit portable resource transitions must reach native barriers. */
		bool mbRequireExplicitTransitions = false;
		/** Paired begin/end transitions must be supported. */
		bool mbRequireSplitTransitions = false;
		/** Draw/dispatch arguments supplied by GPU buffers must be supported. */
		bool mbRequireIndirectCommands = false;
		/** Indirect arguments must support a nonzero first-instance value. */
		bool mbRequireIndirectFirstInstance = false;
		/** Native barriers must support activating resources in overlapping heap memory. */
		bool mbRequireAliasingBarriers = false;
		/** Event, timer and GPU fence queries must be supported; timestamp widths are checked separately. */
		bool mbRequireQueries = false;
		/** Retained compiled libraries must support entry-point shader objects. */
		bool mbRequireShaderLibraries = false;
		/** Compatible native pipeline-cache data must support persistence between runs. */
		bool mbRequirePipelineCachePersistence = false;
		/** The portable subgroup/wave operation set must be supported. */
		bool mbRequireSubgroupOperations = false;
		/** Shader/native buffer addresses must be supported; this does not promise CUDA-runtime pointers. */
		bool mbRequireBufferDeviceAddress = false;

		// Zero/None leaves a numeric/tier requirement unconstrained. Unknown native limits
		// do not satisfy nonzero requirements; requiring a limit does not imply other operations.
		/** Lowest admitted ray tracing tier. */
		EArdaRHIRayTracingTier mMinRayTracingTier = EArdaRHIRayTracingTier::None;
		/** Lowest admitted mesh shader tier. */
		EArdaRHIMeshShaderTier mMinMeshShaderTier = EArdaRHIMeshShaderTier::None;
		/** Lowest admitted work graph tier. */
		EArdaRHIWorkGraphTier mMinWorkGraphTier = EArdaRHIWorkGraphTier::None;
		/** Lowest admitted sampler feedback tier. */
		EArdaRHISamplerFeedbackTier mMinSamplerFeedbackTier = EArdaRHISamplerFeedbackTier::None;
		/** Resource descriptor capacity required by this module. Zero imposes no requirement. */
		uint32_t mMinResourceDescriptors = 0;
		/** Sampler descriptor capacity required by this module. Zero imposes no requirement. */
		uint32_t mMinSamplerDescriptors = 0;
		/** Maximum ray recursion depth the module needs to admit. Zero imposes no requirement. */
		uint32_t mMinRayRecursionDepth = 0;
		/** Ray payload capacity required in bytes; a native unqueryable limit cannot satisfy a nonzero requirement. Zero imposes no requirement. */
		uint32_t mMinRayPayloadSize = 0;
		/** Ray-generation invocation capacity required for one direct dispatch. Zero imposes no requirement. */
		uint32_t mMinRayDispatchInvocations = 0;
		/** Meaningful timestamp bits required on the graphics queue. Zero imposes no requirement. */
		uint32_t mMinGraphicsTimestampValidBits = 0;
		/** Meaningful timestamp bits required on the compute queue. Zero imposes no requirement. */
		uint32_t mMinComputeTimestampValidBits = 0;
		/** Meaningful timestamp bits required on the copy queue. Zero imposes no requirement. */
		uint32_t mMinCopyTimestampValidBits = 0;
		/**
		 * Smallest subgroup width accepted by the module. When either subgroup bound
		 * is nonzero, the complete reported native width interval must be known and
		 * contained in the accepted interval, and subgroup operations must be supported.
		 * This does not assume the provider supports choosing a subgroup width.
		 */
		uint32_t mMinSubgroupSize = 0;
		/** Largest accepted subgroup width; zero leaves the upper bound open. Equal nonzero bounds require one exact native width. */
		uint32_t mMaxSubgroupSize = 0;
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
		/** Native resource and work-dispatch limits used by common admission checks. */
		FArdaRHIDeviceLimits mLimits;
		/** QueryFormatSupport returns authoritative native format facts, including unsupported formats. */
		bool mbFormatSupportReported = false;
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
		/** The graphics pipeline admits the enabled geometry shader stage. */
		bool mbGeometryShaders = false;
		/** The graphics pipeline admits enabled hull/domain tessellation shader stages. */
		bool mbTessellationShaders = false;
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
		/** Indirect draw arguments may contain a nonzero first-instance value. */
		bool mbIndirectFirstInstance = false;
		/** Barriers can establish a new active resource in overlapping heap memory. */
		bool mbAliasingBarriers = false;
		/** Event, timer and GPU fence query paths are implemented. */
		bool mbQueries = false;
		/** Retained compiled libraries can provide entry-point shader objects. */
		bool mbShaderLibraries = false;
		/** Compatible native pipeline-cache data can be persisted between runs. */
		bool mbPipelineCachePersistence = false;

		/** Tests stage support without relying on the ABI-preserved mesh-tier numeric values. */
		[[nodiscard]] bool SupportsMeshShaderTier(EArdaRHIMeshShaderTier Required) const noexcept
		{
			switch (Required)
			{
			case EArdaRHIMeshShaderTier::None:
				return true;
			case EArdaRHIMeshShaderTier::MeshShadersOnly:
				return mMeshShaderTier == EArdaRHIMeshShaderTier::MeshShadersOnly ||
				    mMeshShaderTier == EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
			case EArdaRHIMeshShaderTier::MeshAndAmplificationShaders:
				return mMeshShaderTier == EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
			}
			return false;
		}

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
			Need(R.mbRequireMeshShaders,
			    SupportsMeshShaderTier(EArdaRHIMeshShaderTier::MeshShadersOnly),
			    "mesh shaders");
			Need(R.mbRequireGeometryShaders, mbGeometryShaders, "geometry shaders");
			Need(R.mbRequireTessellationShaders, mbTessellationShaders, "tessellation shaders");
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

			Need(R.mbRequireInlineRayQueries, mRayTracing.mbInlineRayQueries, "inline ray queries");
			Need(R.mbRequireBottomLevelAccelerationStructures,
			    mRayTracing.mbBottomLevel,
			    "bottom-level acceleration structures");
			Need(R.mbRequireTopLevelAccelerationStructures,
			    mRayTracing.mbTopLevel,
			    "top-level acceleration structures");
			Need(R.mbRequireIndirectTopLevelBuild, mRayTracing.mbIndirectTopLevelBuild, "indirect top-level build");
			Need(R.mbRequirePersistentShaderTables, mRayTracing.mbPersistentShaderTables, "persistent shader tables");
			Need(R.mbRequireBindless, mDescriptors.mbBindless, "bindless descriptor tables");
			Need(R.mbRequireRuntimeDescriptorArrays,
			    mDescriptors.mbRuntimeDescriptorArrays,
			    "runtime descriptor arrays");
			Need(R.mbRequirePartiallyBoundDescriptors, mDescriptors.mbPartiallyBound, "partially bound descriptors");
			Need(R.mbRequireUpdateUnusedWhilePending,
			    mDescriptors.mbUpdateUnusedWhilePending,
			    "descriptor update-unused-while-pending");
			Need(R.mbRequireVariableDescriptorCount,
			    mDescriptors.mbVariableDescriptorCount,
			    "variable descriptor count");
			Need(R.mbRequireDirectSamplerHeapIndexing,
			    mDescriptors.mbDirectSamplerHeapIndexing,
			    "direct sampler heap indexing");
			Need(R.mbRequireDescriptorBuffer, mDescriptors.mbDescriptorBuffer, "descriptor buffers");
			Need(R.mbRequireDescriptorHeap, mDescriptors.mbDescriptorHeap, "descriptor heaps");
			Need(R.mbRequireGraphicsQueue, mQueues.mbGraphics, "graphics queue");
			Need(R.mbRequireComputeQueue, mQueues.mbCompute, "compute queue");
			Need(R.mbRequireCopyQueue, mQueues.mbCopy, "copy queue");
			Need(R.mbRequireTimelineSynchronization, mQueues.mbTimelineSynchronization, "timeline synchronization");
			Need(R.mbRequireQueueFamilyOwnershipTransfer,
			    mQueues.mbQueueFamilyOwnershipTransfer,
			    "queue-family ownership transfer");
			Need(R.mbRequireSparseBindingQueue, mQueues.mbSparseBindingQueue, "sparse binding queue");
			Need(R.mbRequireReservedBuffers, mResidency.mbReservedBuffers, "reserved buffers");
			Need(R.mbRequireReservedTexture2D, mResidency.mbReservedTexture2D, "reserved 2D textures");
			Need(R.mbRequireReservedTexture3D, mResidency.mbReservedTexture3D, "reserved 3D textures");
			Need(R.mbRequireAliasedMappings, mResidency.mbAliasedMappings, "aliased sparse mappings");
			Need(R.mbRequireBudgetReservation, mResidency.mbBudgetReservation, "streaming budget reservation");
			Need(R.mbRequireResourceCollections, mbResourceCollections, "resource collections");
			Need(R.mbRequireConservativeRasterization, mbConservativeRasterization, "conservative rasterization");
			Need(R.mbRequireVariableRateShading, mbVariableRateShading, "variable rate shading");
			Need(R.mbRequireVirtualResources, mbVirtualResources, "virtual resources");
			Need(R.mbRequireHeaps, mbHeaps, "explicit heaps");
			Need(R.mbRequireStagingTextures, mbStagingTextures, "staging textures");
			Need(R.mbRequireTextureCopies, mbTextureCopies, "texture copies");
			Need(R.mbRequireTextureResolve, mbTextureResolve, "texture resolve");
			Need(R.mbRequireExplicitTransitions, mbExplicitTransitions, "explicit transitions");
			Need(R.mbRequireSplitTransitions, mbSplitTransitions, "split transitions");
			Need(R.mbRequireIndirectCommands, mbIndirectCommands, "indirect commands");
			Need(R.mbRequireIndirectFirstInstance, mbIndirectFirstInstance, "indirect first instance");
			Need(R.mbRequireAliasingBarriers, mbAliasingBarriers, "aliasing barriers");
			Need(R.mbRequireQueries, mbQueries, "GPU queries");
			Need(R.mbRequireShaderLibraries, mbShaderLibraries, "shader libraries");
			Need(R.mbRequirePipelineCachePersistence, mbPipelineCachePersistence, "pipeline cache persistence");
			Need(R.mbRequireSubgroupOperations, mMachineLearning.mbSubgroupOperations, "subgroup operations");
			Need(R.mbRequireBufferDeviceAddress, mMachineLearning.mbBufferDeviceAddress, "buffer device addresses");
			Need(R.mMinRayTracingTier != EArdaRHIRayTracingTier::None,
			    mRayTracing.GetTier() >= R.mMinRayTracingTier,
			    "ray-tracing tier");
			Need(R.mMinMeshShaderTier != EArdaRHIMeshShaderTier::None,
			    SupportsMeshShaderTier(R.mMinMeshShaderTier),
			    "mesh-shader tier");
			Need(R.mMinWorkGraphTier != EArdaRHIWorkGraphTier::None,
			    mWorkGraphTier >= R.mMinWorkGraphTier,
			    "work-graph tier");
			Need(R.mMinSamplerFeedbackTier != EArdaRHISamplerFeedbackTier::None,
			    mSamplerFeedbackTier >= R.mMinSamplerFeedbackTier,
			    "sampler-feedback tier");
			Need(R.mMinResourceDescriptors != 0,
			    mDescriptors.mMaxResourceDescriptors >= R.mMinResourceDescriptors,
			    "resource descriptor capacity");
			Need(R.mMinSamplerDescriptors != 0,
			    mDescriptors.mMaxSamplerDescriptors >= R.mMinSamplerDescriptors,
			    "sampler descriptor capacity");
			Need(R.mMinRayRecursionDepth != 0,
			    mRayTracing.mMaxRecursionDepth >= R.mMinRayRecursionDepth,
			    "ray recursion depth");
			Need(R.mMinRayPayloadSize != 0, mRayTracing.mMaxRayPayloadSize >= R.mMinRayPayloadSize, "ray payload size");
			Need(R.mMinRayDispatchInvocations != 0,
			    mRayTracing.mMaxRayDispatchInvocations >= R.mMinRayDispatchInvocations,
			    "ray dispatch invocations");
			Need(R.mMinGraphicsTimestampValidBits != 0,
			    mQueues.GetTimestampValidBits(EArdaRHIQueueType::Graphics) >= R.mMinGraphicsTimestampValidBits,
			    "graphics timestamp precision");
			Need(R.mMinComputeTimestampValidBits != 0,
			    mQueues.GetTimestampValidBits(EArdaRHIQueueType::Compute) >= R.mMinComputeTimestampValidBits,
			    "compute timestamp precision");
			Need(R.mMinCopyTimestampValidBits != 0,
			    mQueues.GetTimestampValidBits(EArdaRHIQueueType::Copy) >= R.mMinCopyTimestampValidBits,
			    "copy timestamp precision");
			if (R.mMinSubgroupSize != 0 || R.mMaxSubgroupSize != 0)
			{
				const auto& ML = mMachineLearning;
				const bool Known =
				    ML.mbSubgroupOperations && ML.mSubgroupMinSize != 0 && ML.mSubgroupMaxSize >= ML.mSubgroupMinSize;
				const bool Valid = R.mMaxSubgroupSize == 0 || R.mMinSubgroupSize <= R.mMaxSubgroupSize;
				Need(true,
				    Known && Valid && ML.mSubgroupMinSize >= R.mMinSubgroupSize &&
				        (R.mMaxSubgroupSize == 0 || ML.mSubgroupMaxSize <= R.mMaxSubgroupSize),
				    "subgroup width interval");
			}
			return Report;
		}
	};

	/** Validates portable shape plus reported device limits and format uses; unreported limits are not inferred. */
	[[nodiscard]] FArdaRHIStatus ValidateResourceCapabilities(const FArdaRHITextureDesc& Desc,
	    const FArdaRHICapabilities& Capabilities,
	    const FArdaRHIFormatSupport& FormatSupport) noexcept;

	/** Validates allocation admission; uniform/storage binding-range limits apply to views, not whole buffers. */
	[[nodiscard]] FArdaRHIStatus ValidateResourceCapabilities(const FArdaRHIBufferDesc& Desc,
	    const FArdaRHICapabilities& Capabilities,
	    const FArdaRHIFormatSupport& FormatSupport) noexcept;
}
