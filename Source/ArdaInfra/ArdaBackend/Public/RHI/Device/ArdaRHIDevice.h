/** @file ArdaRHIDevice.h
 * Device creation, capability queries and resource entry points.
 */
#pragma once

#include "RHI/Config/ArdaRHICapabilities.h"
#include "RHI/Device/ArdaRHIDiagnostics.h"
#include "RHI/Interop/ArdaRHINativeResourceImports.h"
#include "RHI/Memory/ArdaGpuAllocator.h"
#include "RHI/Memory/ArdaRHIHeap.h"
#include "RHI/Memory/ArdaRHITiling.h"
#include "RHI/Pipelines/ArdaRHIWorkGraphPipeline.h"
#include "RHI/Resources/ArdaRHIResourceCollection.h"
#include "RHI/Resources/ArdaRHISamplerFeedback.h"
#include "RHI/Scheduling/ArdaRHICommandList.h"
#include "RHI/Scheduling/ArdaRHIGpuFence.h"
#include "RHI/Scheduling/ArdaRHIQueries.h"
#include "RHI/Shaders/ArdaRHIShaderBundle.h"

namespace arda
{
	/** Sizes of the bounded descriptor caches owned by a device. */
	struct FArdaRHICacheStats
	{
		/** Stores the samplers. */
		size_t mSamplers = 0;
		/** Stores the binding layouts. */
		size_t mBindingLayouts = 0;
		/** Stores the input layouts. */
		size_t mInputLayouts = 0;
		/** Stores the graphics pipelines. */
		size_t mGraphicsPipelines = 0;
		/** Stores the compute pipelines. */
		size_t mComputePipelines = 0;
		/** Stores the meshlet pipelines. */
		size_t mMeshletPipelines = 0;
		/** Stores the ray tracing pipelines. */
		size_t mRayTracingPipelines = 0;
		/** Stores the raster states. */
		size_t mRasterStates = 0;
		/** Stores the blend states. */
		size_t mBlendStates = 0;
		/** Stores the depth stencil states. */
		size_t mDepthStencilStates = 0;
	};

	/** Live object and transient native-allocation counts used for lifetime validation. */
	struct FArdaRHIResourceLifetimeStats
	{
		size_t mLiveResources[static_cast<size_t>(EArdaRHIResourceType::Count)]{};
		size_t mResourceDescriptors = 0;
		size_t mSamplerDescriptors = 0;
		size_t mDescriptorSets = 0;
		size_t mPendingSubmissions = 0;

		[[nodiscard]] size_t GetLiveResourceCount(EArdaRHIResourceType Type) const noexcept
		{
			const size_t Index = static_cast<size_t>(Type);
			return Index < static_cast<size_t>(EArdaRHIResourceType::Count) ? mLiveResources[Index] : 0;
		}
	};

	/** Interface for device. */
	class IArdaRHIDevice : public virtual IArdaRHIResource
	{
	public:
		/** Reads this device's qualified CUDA launch mode, architecture, limits and surface support.
         * @return An owned snapshot. A false boolean conversion means no CUDA launch mode;
         * mUnavailableReason explains why. Surface support and its diagnostic are independent.
         * CUDA-disabled builds return an unavailable snapshot while graphics compute remains usable.
         * @ownership The returned value owns its strings; it contains no CUDA handles or mapped memory.
         * @threading Read-only after device initialization; keep the device alive during the call.
         * @errors Unavailable CUDA is reported in the snapshot, not as FArdaRHIStatus or an exception.
         */
		[[nodiscard]] virtual FArdaCudaCapabilities GetCudaCapabilities() const
		{
			return {};
		}

		/**
         * Returns the capabilities.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHICapabilities& GetCapabilities() const noexcept = 0;

		/** Returns native per-format facts without creating resources; zero facts mean unavailable/unreported. */
		[[nodiscard]] virtual FArdaRHIFormatSupport QueryFormatSupport(EArdaRHIFormat) const noexcept
		{
			return {};
		}

		/**
		 * Captures bounded native device status, queue progress, recent markers and available fault data.
		 * Does not submit or wait for GPU work; unavailable native fault facilities are explicitly reported.
		 * Snapshot strings and records are owned by the result. Serialize shutdown with this call.
		 */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIDiagnosticSnapshot> CaptureDiagnosticSnapshot() const
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Native diagnostics are unavailable.")};
		}

		/** Evaluates a future module's required abilities against this device. */
		[[nodiscard]] FArdaRHIFeatureSupportReport CheckFeatureSupport(
		    const FArdaRHIFeatureRequirements& Requirements) const
		{
			return GetCapabilities().Evaluate(Requirements);
		}

		/** Returns Unsupported with every missing ability when requirements fail. */
		[[nodiscard]] FArdaRHIStatus RequireFeatures(const FArdaRHIFeatureRequirements& Requirements) const
		{
			return CheckFeatureSupport(Requirements).ToStatus();
		}

		/**
         * Creates a texture.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureRef> CreateTexture(const FArdaRHITextureDesc& Desc) = 0;

		/**
         * Creates a texture reference.
         * @param Texture The texture.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureReferenceRef> CreateTextureReference(
		    const FArdaRHITextureRef& Texture = {}) = 0;

		/**
         * Performs the set texture reference operation.
         * @param Reference The reference.
         * @param Texture The texture.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus SetTextureReference(const FArdaRHITextureReferenceRef& Reference,
		    const FArdaRHITextureRef& Texture) = 0;

		/**
         * Creates a buffer.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIBufferRef> CreateBuffer(const FArdaRHIBufferDesc& Desc) = 0;

		/**
         * Creates a uniform buffer.
         * @param Desc The desc.
         * @param InitialData The initial data.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIUniformBufferRef> CreateUniformBuffer(
		    const FArdaRHIUniformBufferDesc& Desc,
		    const void* InitialData = nullptr) = 0;

		/**
         * Performs the import native texture operation.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureRef> ImportNativeTexture(
		    const FArdaRHINativeTextureImportDesc& Desc) = 0;

		/**
         * Performs the import native buffer operation.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIBufferRef> ImportNativeBuffer(
		    const FArdaRHINativeBufferImportDesc& Desc) = 0;

		/**
         * Creates a heap.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIHeapRef> CreateHeap(const FArdaRHIHeapDesc& Desc) = 0;

		/** Acquires a texture already bound at an explicit heap offset. The descriptor must be virtual.
		 * Native objects may be reused across graphs; the returned lease retains its parent heap.
		 */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureRef> CreatePlacedTexture(const FArdaRHITextureDesc& Desc,
		    const FArdaRHIHeapRef& Heap,
		    uint64_t Offset)
		{
			auto Resource = CreateTexture(Desc);
			if (Resource)
			{
				Resource.mStatus = BindTextureMemory(Resource.mValue, Heap, Offset);
				if (!Resource.mStatus)
				{
					Resource.mValue.Reset();
				}
			}
			return Resource;
		}

		/** Acquires a buffer already bound at an explicit heap offset. The descriptor must be virtual.
		 * Native objects may be reused across graphs; simultaneous leases remain independent.
		 */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIBufferRef> CreatePlacedBuffer(const FArdaRHIBufferDesc& Desc,
		    const FArdaRHIHeapRef& Heap,
		    uint64_t Offset)
		{
			auto Resource = CreateBuffer(Desc);
			if (Resource)
			{
				Resource.mStatus = BindBufferMemory(Resource.mValue, Heap, Offset);
				if (!Resource.mStatus)
				{
					Resource.mValue.Reset();
				}
			}
			return Resource;
		}

		/** Reads device-wide heap and resource cache statistics without GPU work. */
		[[nodiscard]] virtual FArdaGpuAllocatorStats GetGpuAllocatorStats() const
		{
			return {};
		}

		/** Changes idle cache policy for this device; never invalidates active resources. */
		virtual FArdaRHIStatus SetGpuAllocatorOptions(const FArdaGpuAllocatorOptions&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "GPU allocation caching is unavailable.");
		}

		/** Releases idle cached storage after polling native retirement. Active leases and unproven GPU work remain retained.
         * @return No immediate result; query allocator and lifetime statistics to observe released storage.
         * @ownership Only idle cache ownership is released; active and pending resource ownership is preserved.
         * @errors This call returns no status and does not establish GPU completion. Unproven retirement remains pending.
         * @threading Coordinate with device shutdown; allocator collection synchronizes its internal state. */
		virtual void TrimGpuAllocator()
		{
		}

		/**
         * Creates a staging texture.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIStagingTextureRef> CreateStagingTexture(
		    const FArdaRHIStagingTextureDesc& Desc) = 0;

		/**
         * Performs the map staging texture operation.
         * @param Texture The texture.
         * @param Slice The slice.
         * @param Access The access.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(
		    const FArdaRHIStagingTextureRef& Texture,
		    const FArdaRHITextureSlice& Slice,
		    EArdaRHICpuAccess Access) = 0;

		/**
         * Performs the unmap staging texture operation.
         * @param Texture The texture.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus UnmapStagingTexture(const FArdaRHIStagingTextureRef& Texture) = 0;

		/**
         * Creates a shader resource view.
         * @param Resource The resource.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderResourceViewRef> CreateShaderResourceView(
		    const TArdaRHIRef<IArdaRHIResource>& Resource,
		    const FArdaRHIViewDesc& Desc) = 0;

		/**
         * Creates a unordered access view.
         * @param Resource The resource.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIUnorderedAccessViewRef> CreateUnorderedAccessView(
		    const TArdaRHIRef<IArdaRHIResource>& Resource,
		    const FArdaRHIViewDesc& Desc) = 0;

		/**
         * Creates a sampler.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHISamplerRef> CreateSampler(const FArdaRHISamplerDesc& Desc) = 0;

		/**
         * Creates a shader.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderRef> CreateShader(const FArdaRHIShaderDesc& Desc) = 0;

		/**
         * Creates a shader library.
         * @param Bytecode The bytecode.
         * @param BytecodeSize The bytecode size.
         * @param DebugName The debug name.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderLibraryRef> CreateShaderLibrary(const void* Bytecode,
		    size_t BytecodeSize,
		    const char* DebugName = nullptr) = 0;

		/**
         * Returns the shader from library.
         * @param Library The library.
         * @param EntryPoint The entry point.
         * @param Stage The stage.
         * @param DebugName The debug name.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderRef> GetShaderFromLibrary(
		    const FArdaRHIShaderLibraryRef& Library,
		    const char* EntryPoint,
		    EArdaRHIShaderStage Stage,
		    const char* DebugName = nullptr) = 0;

		/**
         * Creates a input layout.
         * @param Attributes The attributes.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIInputLayoutRef> CreateInputLayout(
		    const eastl::vector<FArdaRHIVertexAttributeDesc>& Attributes) = 0;

		/**
         * Creates a binding layout.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindingLayout(
		    const FArdaRHIBindingLayoutDesc& Desc) = 0;

		/**
         * Creates a bindless layout.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindlessLayout(
		    const FArdaRHIBindlessLayoutDesc& Desc) = 0;

		/**
         * Creates a binding set.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIBindingSetRef> CreateBindingSet(
		    const FArdaRHIBindingSetDesc& Desc) = 0;

		/**
         * Creates a descriptor table.
         * @param Layout The layout.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIDescriptorTableRef> CreateDescriptorTable(
		    const FArdaRHIBindingLayoutRef& Layout) = 0;

		/** Creates a general resource collection retained by the device. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIResourceCollectionRef> CreateResourceCollection(
		    const FArdaRHIResourceCollectionDesc&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Resource collections are unsupported by this device.")};
		}

		/** Replaces one mutable resource-collection member. */
		virtual FArdaRHIStatus UpdateResourceCollection(const FArdaRHIResourceCollectionRef&,
		    uint32_t,
		    const FArdaRHIResourceCollectionItem&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Mutable resource collections are unsupported by this device.");
		}

		/**
         * Publishes a resized table version without changing previously recorded versions.
         * @param Table Bindless descriptor table created by this device.
         * @param NewSize Nonzero logical capacity no larger than the layout's maximum.
         * @param bKeepContents Preserve in-range entries when true; otherwise clear the new version.
         * @return Success after replacement, InvalidArgument for size/descriptor errors,
         * WrongDevice for invalid ownership, or a native allocation failure. Failure preserves the previous version.
         * @ownership The new version retains its resources; recorded versions independently retain
         * their previous dependencies through submission completion, including removed entries.
         * @threading Table updates and recording snapshots are serialized by the table mutex.
         * @errors A null, foreign-device or incompatible table is rejected with WrongDevice.
         */
		virtual FArdaRHIStatus ResizeDescriptorTable(const FArdaRHIDescriptorTableRef& Table,
		    uint32_t NewSize,
		    bool bKeepContents = true) = 0;

		/**
         * Replaces one descriptor by publishing a retained table version.
         * @param Table Bindless descriptor table created by this device.
         * @param Item Same-device native resource and a declared slot/type/array element in range.
         * @return Success after replacement, WrongDevice for invalid ownership, InvalidArgument
         * for an undeclared slot/type or invalid range, or a native allocation failure.
         * Failure does not publish a partially changed table.
         * @ownership A successful write retains Item.mResource immediately. Recorded versions keep
         * their dependencies when subsequent writes replace entries; submission retains them until
         * its own queue completes. No unsafe-lifetime opt-in is required.
         * @threading Table writes and command-list snapshots are serialized by the table mutex;
         * resource data still needs ordinary GPU barriers and queue ordering.
         * @errors WrongDevice rejects null/foreign resources and incompatible tables;
         * InvalidArgument rejects undeclared slots or out-of-range array elements.
         */
		virtual FArdaRHIStatus WriteDescriptorTable(const FArdaRHIDescriptorTableRef& Table,
		    const FArdaRHIBindingItem& Item) = 0;

		/**
         * Creates a framebuffer.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIFramebufferRef> CreateFramebuffer(
		    const FArdaRHIFramebufferDesc& Desc) = 0;

		/**
         * Creates a graphics pipeline.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIGraphicsPipelineRef> CreateGraphicsPipeline(
		    const FArdaRHIGraphicsPipelineDesc& Desc) = 0;

		/**
         * Creates a compute pipeline.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIComputePipelineRef> CreateComputePipeline(
		    const FArdaRHIComputePipelineDesc& Desc) = 0;

		/**
         * Creates a meshlet pipeline.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIMeshletPipelineRef> CreateMeshletPipeline(
		    const FArdaRHIMeshletPipelineDesc& Desc) = 0;

		/**
         * Creates a raster state.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIRasterStateRef> CreateRasterState(
		    const FArdaRHIRasterState& Desc) = 0;

		/**
         * Creates a blend state.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIBlendStateRef> CreateBlendState(
		    const FArdaRHIBlendState& Desc) = 0;

		/**
         * Creates a depth stencil state.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIDepthStencilStateRef> CreateDepthStencilState(
		    const FArdaRHIDepthStencilState& Desc) = 0;

		/**
         * Creates a accel struct.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIAccelStructRef> CreateAccelStruct(
		    const FArdaRHIAccelStructDesc& Desc) = 0;

		/** Returns result and scratch sizes for an acceleration-structure descriptor. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements>
		GetAccelStructBuildMemoryRequirements(const FArdaRHIAccelStructDesc& Desc) = 0;

		/** Returns compacted size after a compaction-enabled build completes. */
		[[nodiscard]] virtual TArdaRHIResult<uint64_t> GetAccelStructCompactedSize(
		    const FArdaRHIAccelStructRef& AccelStruct) = 0;

		/**
         * Creates a opacity micromap.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIOpacityMicromapRef> CreateOpacityMicromap(
		    const FArdaRHIOpacityMicromapDesc& Desc) = 0;

		/** Returns compacted size after a compaction-enabled micromap build completes. */
		[[nodiscard]] virtual TArdaRHIResult<uint64_t> GetOpacityMicromapCompactedSize(
		    const FArdaRHIOpacityMicromapRef&)
		{
			return {0,
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Opacity-micromap compaction is unsupported by this device.")};
		}

		/**
         * Creates a ray tracing pipeline.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIRayTracingPipelineRef> CreateRayTracingPipeline(
		    const FArdaRHIRayTracingPipelineDesc& Desc) = 0;

		/**
         * Creates a shader table.
         * @param Pipeline The pipeline.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderTableRef> CreateShaderTable(
		    const FArdaRHIRayTracingPipelineRef& Pipeline,
		    const FArdaRHIShaderTableDesc& Desc) = 0;

		/** Writes or replaces one complete shader-table record. */
		virtual FArdaRHIStatus SetShaderTableRecord(const FArdaRHIShaderTableRef& Table,
		    const FArdaRHIShaderTableRecordDesc& Record) = 0;

		/** Makes pending shader-table writes visible to dispatch. */
		virtual FArdaRHIStatus CommitShaderTable(const FArdaRHIShaderTableRef& Table) = 0;

		/**
         * Performs the set shader table ray generation operation.
         * @param Table The table.
         * @param ExportName The export name.
         * @param Bindings The bindings.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus SetShaderTableRayGeneration(const FArdaRHIShaderTableRef& Table,
		    const char* ExportName,
		    const FArdaRHIBindingSetRef& Bindings = {}) = 0;

		/**
         * Performs the add shader table miss operation.
         * @param Table The table.
         * @param ExportName The export name.
         * @param Bindings The bindings.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<int> AddShaderTableMiss(const FArdaRHIShaderTableRef& Table,
		    const char* ExportName,
		    const FArdaRHIBindingSetRef& Bindings = {}) = 0;

		/**
         * Performs the add shader table hit group operation.
         * @param Table The table.
         * @param ExportName The export name.
         * @param Bindings The bindings.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<int> AddShaderTableHitGroup(const FArdaRHIShaderTableRef& Table,
		    const char* ExportName,
		    const FArdaRHIBindingSetRef& Bindings = {}) = 0;

		/**
         * Performs the add shader table callable operation.
         * @param Table The table.
         * @param ExportName The export name.
         * @param Bindings The bindings.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<int> AddShaderTableCallable(const FArdaRHIShaderTableRef& Table,
		    const char* ExportName,
		    const FArdaRHIBindingSetRef& Bindings = {}) = 0;

		/**
         * Creates a sampler feedback texture.
         * @param PairedTexture The paired texture.
         * @param Desc The desc.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHISamplerFeedbackTextureRef> CreateSamplerFeedbackTexture(
		    const FArdaRHITextureRef& PairedTexture,
		    const FArdaRHISamplerFeedbackTextureDesc& Desc) = 0;

		/** Creates a work-graph executable on a backend with a reported tier. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIWorkGraphPipelineRef> CreateWorkGraphPipeline(
		    const FArdaRHIWorkGraphPipelineDesc&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Work graphs are unsupported by this device.")};
		}

		/** Creates a mutable or persistent shader bundle. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIShaderBundleRef> CreateShaderBundle(
		    const FArdaRHIShaderBundleDesc&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Shader bundles are unsupported by this device.")};
		}

		/** Replaces records stored in a shader bundle. */
		virtual FArdaRHIStatus SetShaderBundleRecords(const FArdaRHIShaderBundleRef&,
		    const eastl::vector<FArdaRHIShaderBundleRecord>&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Shader bundles are unsupported by this device.");
		}

		/**
         * Creates a event query.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIEventQueryRef> CreateEventQuery() = 0;

		/**
         * Creates a timer query.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHITimerQueryRef> CreateTimerQuery() = 0;

		/**
         * Creates a GPU fence.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIGpuFenceRef> CreateGpuFence() = 0;

		/**
         * Performs the signal event query operation.
         * @param Query The query.
         * @param Queue The queue.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus SignalEventQuery(const FArdaRHIEventQueryRef& Query, EArdaRHIQueueType Queue) = 0;

		/**
         * Performs the poll event query operation.
         * @param Query The query.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<bool> PollEventQuery(const FArdaRHIEventQueryRef& Query) = 0;

		/**
         * Performs the wait event query operation.
         * @param Query The query.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus WaitEventQuery(const FArdaRHIEventQueryRef& Query) = 0;

		/**
         * Performs the reset event query operation.
         * @param Query The query.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus ResetEventQuery(const FArdaRHIEventQueryRef& Query) = 0;

		/**
         * Performs the poll timer query operation.
         * @param Query The query.
         * @return The requested value and its operation status.
         */
		/** Nonblocking readiness check for the current query use; never waits for GPU completion. */
		[[nodiscard]] virtual TArdaRHIResult<bool> PollTimerQuery(const FArdaRHITimerQueryRef& Query) = 0;

		/**
         * Returns the timer query seconds.
         * @param Query The query.
         * @return The requested value and its operation status.
         */
		/** Returns completed elapsed seconds; returns InvalidState immediately while pending. */
		[[nodiscard]] virtual TArdaRHIResult<float> GetTimerQuerySeconds(const FArdaRHITimerQueryRef& Query) = 0;

		/**
         * Performs the reset timer query operation.
         * @param Query The query.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus ResetTimerQuery(const FArdaRHITimerQueryRef& Query) = 0;

		/**
         * Performs the signal GPU fence operation.
         * @param Fence The fence.
         * @param Queue The queue.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus SignalGpuFence(const FArdaRHIGpuFenceRef& Fence, EArdaRHIQueueType Queue) = 0;

		/**
         * Performs the poll GPU fence operation.
         * @param Fence The fence.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<bool> PollGpuFence(const FArdaRHIGpuFenceRef& Fence) = 0;

		/**
         * Performs the wait GPU fence operation.
         * @param Fence The fence.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus WaitGpuFence(const FArdaRHIGpuFenceRef& Fence) = 0;

		/**
         * Performs the reset GPU fence operation.
         * @param Fence The fence.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus ResetGpuFence(const FArdaRHIGpuFenceRef& Fence) = 0;

		/**
         * Creates a command list.
         * @param Queue The queue.
         * @param bImmediateExecution The b immediate execution.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHICommandListRef> CreateCommandList(
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics,
		    bool bImmediateExecution = false) = 0;

		/**
         * Performs the execute command list operation.
         * @param CommandList The command list.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<uint64_t> ExecuteCommandList(
		    const FArdaRHICommandListRef& CommandList) = 0;

		/**
         * Performs the execute command lists operation.
         * @param CommandLists The command lists.
         * @param Queue The queue.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<uint64_t> ExecuteCommandLists(
		    const eastl::vector<FArdaRHICommandListRef>& CommandLists,
		    EArdaRHIQueueType Queue) = 0;

		/**
         * Performs the queue wait operation.
         * @param WaitQueue The wait queue.
         * @param ExecutionQueue The execution queue.
         * @param Instance The instance.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus QueueWait(EArdaRHIQueueType WaitQueue,
		    EArdaRHIQueueType ExecutionQueue,
		    uint64_t Instance) = 0;

		/**
         * Queries texture allocation requirements without allocating or binding GPU memory.
         * The backend may create a temporary unbound native resource. Creation flags, including
         * virtual, sparse, and CUDA interoperability flags, are taken from the descriptor.
         * @param Desc The proposed texture descriptor.
         * @return Required size, alignment, compatible memory types, and operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> QueryTextureMemoryRequirements(
		    const FArdaRHITextureDesc& Desc) = 0;

		/**
         * Queries buffer allocation requirements without allocating or binding GPU memory.
         * The backend may create a temporary unbound native resource.
         * @param Desc The proposed buffer descriptor, including all intended creation flags.
         * @return Required size, alignment, compatible memory types, and operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> QueryBufferMemoryRequirements(
		    const FArdaRHIBufferDesc& Desc) = 0;

		/**
         * Returns the texture memory requirements.
         * @param Texture The texture.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(
		    const FArdaRHITextureRef& Texture) = 0;

		/**
         * Returns the buffer memory requirements.
         * @param Buffer The buffer.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(
		    const FArdaRHIBufferRef& Buffer) = 0;

		/**
         * Returns the accel struct memory requirements.
         * @param AccelStruct The accel struct.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetAccelStructMemoryRequirements(
		    const FArdaRHIAccelStructRef& AccelStruct) = 0;

		/**
         * Performs the bind texture memory operation.
         * @param Texture The texture.
         * @param Heap The heap.
         * @param Offset The offset.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus BindTextureMemory(const FArdaRHITextureRef& Texture,
		    const FArdaRHIHeapRef& Heap,
		    uint64_t Offset) = 0;

		/**
         * Performs the bind buffer memory operation.
         * @param Buffer The buffer.
         * @param Heap The heap.
         * @param Offset The offset.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus BindBufferMemory(const FArdaRHIBufferRef& Buffer,
		    const FArdaRHIHeapRef& Heap,
		    uint64_t Offset) = 0;

		/**
         * Performs the bind accel struct memory operation.
         * @param AccelStruct The accel struct.
         * @param Heap The heap.
         * @param Offset The offset.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus BindAccelStructMemory(const FArdaRHIAccelStructRef& AccelStruct,
		    const FArdaRHIHeapRef& Heap,
		    uint64_t Offset) = 0;

		/**
         * Returns the texture tiling.
         * @param Texture The texture.
         * @return The requested value and its operation status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureTiling> GetTextureTiling(
		    const FArdaRHITextureRef& Texture) = 0;

		/**
         * Updates sparse texture mappings, retaining replaced backing until native retirement is proven.
         * Synchronize other-queue accesses before remapping. Vulkan rejects overlapping ranges within one batch and spatial tiles while an opaque prefix is active. CommitReservedResource with zero bytes fully unbinds before a mode switch; explicit opaque mip-tail ranges remain interoperable.
         * @param Texture Reserved texture whose mappings are changed.
         * @param Mappings Tile ranges, heap offsets and optional backing heaps; an absent heap unmaps the range.
         * @param Queue Queue on which mapping changes are ordered.
         * @return Admission or native mapping/wait status.
         * @ownership Live ranges retain their heaps; accepted operations retain previous and replacement backing until retirement proof.
         * @errors Pre-submit rejection preserves prior mappings. A recoverable wait/signal failure can follow native acceptance; accepted state remains owned and GC/idle retries retirement.
         * @threading Serialize mapping changes and order all other-queue accesses before remapping.
         */
		virtual FArdaRHIStatus UpdateTextureTileMappings(const FArdaRHITextureRef& Texture,
		    const eastl::vector<FArdaRHITextureTileMapping>& Mappings,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics) = 0;

		/**
         * Updates sparse buffer mappings, splitting prior range ownership on partial replacement or unmap.
         * @param Buffer Reserved buffer whose mappings are changed.
         * @param Mappings Buffer tile ranges and optional backing heaps.
         * @param Queue Queue on which mapping changes are ordered.
         * @return Admission or native mapping/wait status.
         * @ownership Live mappings and accepted pending work retain their backing heaps until retirement proof.
         * @errors Pre-submit rejection preserves prior mappings. A recoverable wait/signal failure can follow native acceptance; GC/idle retries retirement without freeing live backing.
         * @threading Serialize mapping changes and order all other-queue accesses before remapping.
         */
		virtual FArdaRHIStatus UpdateBufferTileMappings(const FArdaRHIBufferRef& Buffer,
		    const eastl::vector<FArdaRHIBufferTileMapping>& Mappings,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Copy)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Sparse buffers are unsupported by this device.");
		}

		/**
         * Grows or shrinks a reserved resource's committed prefix while preserving still-mapped bytes.
         * Clamp to resource capacity before tile alignment, so UINT64_MAX requests full capacity. Growth fills only missing ranges; shrinking unmaps the suffix. Recommitted, previously unmapped bytes are undefined.
         * Vulkan returns Unsupported for a nonzero opaque image prefix while spatial tiles remain mapped; fully unbind with zero bytes before switching modes. Buffers and explicit opaque mip tails remain interoperable.
         * @param Resource Reserved texture or buffer to commit.
         * @param ByteCount Desired prefix byte count; zero fully unbinds and UINT64_MAX requests full capacity.
         * @param Queue Queue on which mapping changes are ordered.
         * @return Admission or native mapping/wait status.
         * @ownership Live mapped ranges retain backing; old and replacement owners survive accepted pending operations until completion or device-loss proof.
         * @errors Pre-submit failure preserves mappings. A post-acceptance wait/signal error returns failure while retaining the accepted mapping; GC/idle retries retirement. Unproven final-shutdown work is quarantined.
         * @threading Serialize mapping changes and synchronize all other-queue accesses before remapping.
         */
		virtual FArdaRHIStatus CommitReservedResource(const FArdaRHIResourceRef& Resource,
		    uint64_t ByteCount,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Copy)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Reserved-resource commit is unsupported by this device.");
		}

		/** Returns current native streaming budget telemetry. The Boolean selects local memory when true
         * (the default), or non-local memory when false. These observations can change immediately.
         * @return An owned budget snapshot and its query status.
         * @ownership The returned snapshot owns its values and retains no native allocations.
         * @errors Unsupported providers return Unsupported; native query failures return their status.
         * @threading Coordinate with device shutdown; this query establishes no GPU ordering. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIStreamingBudget> QueryStreamingBudget(bool = true) const
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Streaming budget telemetry is unsupported by this device.")};
		}

		/** Requests a native memory-budget reservation where supported. */
		virtual FArdaRHIStatus SetStreamingBudgetReservation(uint64_t, bool = true)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Streaming budget reservation is unsupported by this device.");
		}

		/**
         * Performs the query work graph support operation.
         * @return A status describing whether the operation succeeded.
         */
		[[nodiscard]] virtual FArdaRHIStatus QueryWorkGraphSupport() const = 0;

		/**
         * Performs the query shader bundle support operation.
         * @return A status describing whether the operation succeeded.
         */
		[[nodiscard]] virtual FArdaRHIStatus QueryShaderBundleSupport() const = 0;

		/**
         * Performs the query custom present support operation.
         * @return A status describing whether the operation succeeded.
         */
		[[nodiscard]] virtual FArdaRHIStatus QueryCustomPresentSupport() const = 0;

		/**
         * Performs the query stream source support operation.
         * @return A status describing whether the operation succeeded.
         */
		[[nodiscard]] virtual FArdaRHIStatus QueryStreamSourceSupport() const = 0;

		/** Evicts all descriptor-cached objects; outstanding caller references remain valid. */
		virtual void TrimDescriptorCaches() = 0;

		/**
         * Returns the descriptor cache stats.
         * @return The requested value.
         */
		[[nodiscard]] virtual FArdaRHICacheStats GetDescriptorCacheStats() const noexcept = 0;

		/** Returns live wrapper and native transient-allocation diagnostics. */
		[[nodiscard]] virtual FArdaRHIResourceLifetimeStats GetResourceLifetimeStats() const noexcept
		{
			return {};
		}

		/**
         * Performs the wait for idle operation.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus WaitForIdle() = 0;

		/** Waits for a token returned by this device's ExecuteCommandList. Does not wait for later submissions. */
		virtual FArdaRHIStatus WaitForSubmission(uint64_t Submission)
		{
			(void)Submission;
			return WaitForIdle();
		}

		/** Nonblocking completion check for a submission from this device. */
		[[nodiscard]] virtual TArdaRHIResult<bool> PollSubmission(uint64_t Submission)
		{
			(void)Submission;
			return {false, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Submission polling is unavailable.")};
		}

		/**
         * Flushes dirty backend-native pipeline cache data and permanently
         * detaches disk persistence from this device wrapper. Idempotent.
         */
		virtual void FlushAndDisablePipelineCachePersistence() noexcept = 0;

		/** Performs the run garbage collection operation. */
		virtual void RunGarbageCollection() = 0;
	};
}
