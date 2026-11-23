/** @file ArdaRHICapabilities.h
 * Declares structured desktop-GPU capabilities and feature requirements.
 */

#pragma once

#include "RHI/Config/ArdaBackendFeatures.h"
#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Scheduling/ArdaRHIQueueTypes.h"

#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <cstddef>
#include <cstdint>

namespace arda
{
	// ================================
	// Device limits, feature tiers and formats
	// ================================

	struct FArdaRHITextureDesc;
	struct FArdaRHIBufferDesc;

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

	// ================================
	// Ray tracing capabilities
	// ================================

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

	// ================================
	// Descriptors and queues
	// ================================

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

	// ================================
	// Residency and machine learning capabilities
	// ================================

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

	// ================================
	// Device capabilities and feature evaluation
	// ================================

	/** Provider-side device facts used to validate RHI commands and qualify published feature values. */
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
		/** Vertex and other pre-rasterization shaders may write storage buffers and use atomics. */
		bool mbVertexShaderStoresAndAtomics = false;
		/** Pixel shaders may write storage buffers and use atomics. */
		bool mbFragmentShaderStoresAndAtomics = false;
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

		/** Adapter features translated into Arda names by this device's provider. */
		FArdaBackendFeatureListRef mBackendFeatures;

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

		/** Adds qualified RHI abilities to the provider's translated feature list and publishes an immutable snapshot. */
		void PublishBackendFeatures(eastl::shared_ptr<FArdaBackendFeatureList> Features);

		/** Returns this device's snapshot, deriving a portable list for providers that supply only RHI facts. */
		[[nodiscard]] FArdaBackendFeatureListRef GetBackendFeatureList() const;

		/** Evaluates a conjunction of named requirements against the published values. */
		[[nodiscard]] FArdaRHIFeatureSupportReport Evaluate(const FArdaRHIFeatureRequirements& Requirements) const;

	private:
		void AddPortableFeatures(FArdaBackendFeatureList& Features) const;
	};

	// ================================
	// Resource capability validation
	// ================================

	/** Validates portable shape plus reported device limits and format uses; unreported limits are not inferred. */
	[[nodiscard]] FArdaRHIStatus ValidateResourceCapabilities(const FArdaRHITextureDesc& Desc,
	    const FArdaRHICapabilities& Capabilities,
	    const FArdaRHIFormatSupport& FormatSupport) noexcept;

	/** Validates allocation admission; uniform/storage binding-range limits apply to views, not whole buffers. */
	[[nodiscard]] FArdaRHIStatus ValidateResourceCapabilities(const FArdaRHIBufferDesc& Desc,
	    const FArdaRHICapabilities& Capabilities,
	    const FArdaRHIFormatSupport& FormatSupport) noexcept;
}
