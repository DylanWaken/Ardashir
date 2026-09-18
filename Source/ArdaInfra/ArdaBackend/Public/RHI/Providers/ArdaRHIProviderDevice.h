/** Device contract implemented by backend providers. */
#pragma once

#include "RHI/Providers/ArdaRHIProviderCommandList.h"

namespace arda
{
	class IArdaRHIProviderDevice
	{
	public:
		[[nodiscard]] virtual FArdaCudaCapabilities GetCudaCapabilities() const
		{
			return {};
		}

		virtual ~IArdaRHIProviderDevice() = default;

		/** Captures bounded native diagnostics without GPU submission or waits. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIDiagnosticSnapshot> CaptureDiagnosticSnapshot() const
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Native diagnostics are unavailable.")};
		}

		[[nodiscard]] virtual const FArdaRHICapabilities& GetCapabilities() const noexcept = 0;

		/** Queries independent native format facts; unknown/unsupported formats return an empty report. */
		[[nodiscard]] virtual FArdaRHIFormatSupport QueryFormatSupport(EArdaRHIFormat) const noexcept
		{
			return {};
		}

		[[nodiscard]] virtual EArdaRHINativeResourceType GetTextureImportType() const noexcept = 0;
		[[nodiscard]] virtual EArdaRHINativeResourceType GetBufferImportType() const noexcept = 0;

		[[nodiscard]] virtual FArdaProviderObjectResult CreateTexture(const FArdaRHITextureDesc& Desc) = 0;

		[[nodiscard]] virtual FArdaProviderObjectResult CreateSamplerFeedbackTexture(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHISamplerFeedbackTextureDesc&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Sampler feedback is unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual FArdaProviderObjectResult CreateBuffer(const FArdaRHIBufferDesc& Desc) = 0;

		/** Installs a weakly captured allocator bridge before resource creation begins.
		 * The callback must call the raw CreateBuffer factory on a cache miss, not AllocateBuffer.
		 */
		void SetBufferAllocator(
		    eastl::function<FArdaProviderObjectResult(const FArdaRHIBufferDesc&, EArdaRHIQueueType)> Allocator)
		{
			mBufferAllocator = eastl::move(Allocator);
		}

		/** Allocates internal upload, readback and scratch buffers through the device cache.
		 * Providers use this entry point internally; CreateBuffer remains the native factory.
		 */
		[[nodiscard]] FArdaProviderObjectResult AllocateBuffer(const FArdaRHIBufferDesc& Desc,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics)
		{
			return mBufferAllocator ? mBufferAllocator(Desc, Queue) : CreateBuffer(Desc);
		}

		[[nodiscard]] virtual FArdaProviderObjectResult CreateHeap(const FArdaRHIHeapDesc& Desc) = 0;

		/** Called only for an idle cached object, after all command/submission leases retired.
		 * Return true only when actual native state and queue ownership honor a fresh descriptor.
		 * This must not submit GPU work or pretend to reset a native resource's state.
		 */
		[[nodiscard]] virtual bool CanReuseBuffer(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&) const noexcept
		{
			return false;
		}

		/** Tests idle buffer state and ownership for internal work on the requested queue.
		 * Non-graphics queues opt out until the provider implements their ownership rules.
		 */
		[[nodiscard]] virtual bool CanReuseBufferForQueue(const FArdaProviderObjectRef& Object,
		    const FArdaRHIBufferDesc& Desc,
		    EArdaRHIQueueType Queue) const noexcept
		{
			return Queue == EArdaRHIQueueType::Graphics && CanReuseBuffer(Object, Desc);
		}

		/** Tests native image state and ownership before an idle object is leased again.
		 * Providers without an explicit state-safe reuse implementation opt out by default.
		 */
		[[nodiscard]] virtual bool CanReuseTexture(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&) const noexcept
		{
			return false;
		}

		/** Queries exact descriptor requirements without allocating or binding GPU memory. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> QueryTextureMemoryRequirements(
		    const FArdaRHITextureDesc&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Texture descriptor memory queries are unsupported by this backend provider.")};
		}

		/** Queries exact descriptor requirements without allocating or binding GPU memory. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> QueryBufferMemoryRequirements(
		    const FArdaRHIBufferDesc&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Buffer descriptor memory queries are unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(
		    const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc) = 0;
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(
		    const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc) = 0;
		virtual FArdaRHIStatus BindTextureMemory(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset) = 0;
		virtual FArdaRHIStatus BindBufferMemory(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset) = 0;

		[[nodiscard]] virtual TArdaRHIResult<FArdaRHITextureTiling> GetTextureTiling(const FArdaProviderObjectRef&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Tiled textures are unsupported by this backend provider.")};
		}

		virtual FArdaRHIStatus UpdateTextureTileMappings(const FArdaProviderObjectRef&,
		    const eastl::vector<FArdaProviderTextureTileMapping>&,
		    EArdaRHIQueueType)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Tiled textures are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus UpdateBufferTileMappings(const FArdaProviderObjectRef&,
		    const eastl::vector<FArdaProviderBufferTileMapping>&,
		    EArdaRHIQueueType)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Sparse buffers are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus CommitReservedResource(const FArdaProviderObjectRef&, bool, uint64_t, EArdaRHIQueueType)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Reserved-resource commit is unsupported by this backend provider.");
		}

		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIStreamingBudget> QueryStreamingBudget(bool) const
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Streaming budgets are unsupported by this backend provider.")};
		}

		virtual FArdaRHIStatus SetStreamingBudgetReservation(uint64_t, bool)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Streaming budget reservation is unsupported by this backend provider.");
		}

		[[nodiscard]] virtual FArdaProviderObjectResult CreateStagingTexture(
		    const FArdaRHIStagingTextureDesc& Desc) = 0;
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(
		    const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureSlice& Slice,
		    EArdaRHICpuAccess Access) = 0;
		virtual FArdaRHIStatus UnmapStagingTexture(const FArdaProviderObjectRef& Texture) = 0;

		/** Maps a host-visible native buffer range. */
		[[nodiscard]] virtual TArdaRHIResult<void*> MapBuffer(const FArdaProviderObjectRef& Buffer,
		    uint64_t Offset,
		    size_t Size) = 0;

		/** Unmaps a native buffer previously returned by MapBuffer. */
		virtual void UnmapBuffer(const FArdaProviderObjectRef& Buffer) noexcept = 0;
		[[nodiscard]] virtual FArdaProviderObjectResult ImportTexture(const FArdaRHINativeTextureImportDesc& Desc) = 0;
		[[nodiscard]] virtual FArdaProviderObjectResult ImportBuffer(const FArdaRHINativeBufferImportDesc& Desc) = 0;
		[[nodiscard]] virtual FArdaProviderObjectResult CreateSampler(const FArdaRHISamplerDesc& Desc) = 0;
		[[nodiscard]] virtual FArdaProviderObjectResult CreateShader(const FArdaRHIShaderDesc& Desc) = 0;
		[[nodiscard]] virtual FArdaProviderObjectResult CreateBindingLayout(const FArdaRHIBindingLayoutDesc& Desc) = 0;

		[[nodiscard]] virtual FArdaProviderObjectResult CreateBindlessLayout(const FArdaRHIBindlessLayoutDesc&,
		    const FArdaRHIBindingLayoutDesc& NativeDesc)
		{
			return CreateBindingLayout(NativeDesc);
		}

		[[nodiscard]] virtual FArdaProviderObjectResult CreateBindingSet(const FArdaRHIBindingSetDesc& Desc,
		    const FArdaProviderObjectRef& Layout,
		    const eastl::vector<FArdaProviderBinding>& Bindings) = 0;
		[[nodiscard]] virtual FArdaProviderObjectResult CreateFramebuffer(
		    const FArdaProviderFramebufferCreateInfo& Info) = 0;
		[[nodiscard]] virtual FArdaProviderObjectResult CreateGraphicsPipeline(
		    const FArdaProviderGraphicsPipelineCreateInfo& Info) = 0;
		[[nodiscard]] virtual FArdaProviderObjectResult CreateComputePipeline(
		    const FArdaProviderComputePipelineCreateInfo& Info) = 0;

		[[nodiscard]] virtual FArdaProviderObjectResult CreateMeshletPipeline(
		    const FArdaProviderMeshletPipelineCreateInfo&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Mesh shaders are unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual FArdaProviderObjectResult CreateRayTracingPipeline(
		    const FArdaProviderRayTracingPipelineCreateInfo&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Ray tracing is unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual FArdaProviderObjectResult CreateWorkGraphPipeline(
		    const FArdaProviderWorkGraphPipelineCreateInfo&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Work graphs are unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual FArdaProviderObjectResult CreateShaderTable(const FArdaProviderObjectRef&,
		    const FArdaRHIShaderTableDesc&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Shader tables are unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements>
		GetAccelStructBuildMemoryRequirements(const FArdaRHIAccelStructDesc&,
		    const eastl::vector<FArdaProviderRayTracingGeometry>&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Acceleration structures are unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual FArdaProviderObjectResult CreateAccelStruct(const FArdaRHIAccelStructDesc&,
		    const FArdaRHIAccelStructMemoryRequirements&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Acceleration structures are unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual TArdaRHIResult<uint64_t> GetAccelStructCompactedSize(const FArdaProviderObjectRef&)
		{
			return {0,
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Acceleration-structure compaction is unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual uint64_t GetAccelStructDeviceAddress(const FArdaProviderObjectRef&) const noexcept
		{
			return 0;
		}

		[[nodiscard]] virtual FArdaProviderObjectResult CreateOpacityMicromap(const FArdaRHIOpacityMicromapDesc&,
		    const FArdaProviderObjectRef&,
		    const FArdaProviderObjectRef&)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Opacity micromaps are unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual TArdaRHIResult<uint64_t> GetOpacityMicromapCompactedSize(const FArdaProviderObjectRef&)
		{
			return {0,
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Opacity-micromap compaction is unsupported by this backend provider.")};
		}

		[[nodiscard]] virtual uint64_t GetOpacityMicromapDeviceAddress(const FArdaProviderObjectRef&) const noexcept
		{
			return 0;
		}

		virtual FArdaRHIStatus SetShaderTableRecord(const FArdaProviderObjectRef&,
		    const FArdaRHIShaderTableRecordDesc&,
		    const FArdaProviderObjectRef&,
		    const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Complete shader-table records are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus CommitShaderTable(const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Explicit shader-table commits are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus SetShaderTableRayGeneration(const FArdaProviderObjectRef&,
		    const char*,
		    const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Shader tables are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus AddShaderTableEntry(const FArdaProviderObjectRef&,
		    const char*,
		    const FArdaProviderObjectRef&,
		    uint32_t)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Shader tables are unsupported by this backend provider.");
		}

		/** Creates a native queue-completion event query. */
		[[nodiscard]] virtual FArdaProviderObjectResult CreateEventQuery()
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Event queries are unsupported by this backend provider.")};
		}

		/** Creates a native timestamp query pair. */
		[[nodiscard]] virtual FArdaProviderObjectResult CreateTimerQuery()
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Timer queries are unsupported by this backend provider.")};
		}

		/** Creates a native GPU queue fence. */
		[[nodiscard]] virtual FArdaProviderObjectResult CreateGpuFence()
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "GPU fences are unsupported by this backend provider.")};
		}

		/** Inserts an event marker after all prior work on the queue. */
		virtual FArdaRHIStatus SignalEventQuery(const FArdaProviderObjectRef&, EArdaRHIQueueType)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Event queries are unsupported by this backend provider.");
		}

		/** Tests whether the native event marker has completed. */
		[[nodiscard]] virtual TArdaRHIResult<bool> PollEventQuery(const FArdaProviderObjectRef&)
		{
			return {false,
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Event queries are unsupported by this backend provider.")};
		}

		/** Waits for the native event marker without idling unrelated queues. */
		virtual FArdaRHIStatus WaitEventQuery(const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Event queries are unsupported by this backend provider.");
		}

		/** Rearms a completed native event query. */
		virtual FArdaRHIStatus ResetEventQuery(const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Event queries are unsupported by this backend provider.");
		}

		/** Tests whether both native timer timestamps are available. */
		[[nodiscard]] virtual TArdaRHIResult<bool> PollTimerQuery(const FArdaProviderObjectRef&)
		{
			return {false,
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Timer queries are unsupported by this backend provider.")};
		}

		/** Returns the elapsed native timestamp interval in seconds. */
		[[nodiscard]] virtual TArdaRHIResult<float> GetTimerQuerySeconds(const FArdaProviderObjectRef&)
		{
			return {0.f,
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Timer queries are unsupported by this backend provider.")};
		}

		/** Rearms a completed native timer query. */
		virtual FArdaRHIStatus ResetTimerQuery(const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Timer queries are unsupported by this backend provider.");
		}

		/** Inserts a native fence signal after all prior work on the queue. */
		virtual FArdaRHIStatus SignalGpuFence(const FArdaProviderObjectRef&, EArdaRHIQueueType)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "GPU fences are unsupported by this backend provider.");
		}

		/** Tests whether the native GPU fence has completed. */
		[[nodiscard]] virtual TArdaRHIResult<bool> PollGpuFence(const FArdaProviderObjectRef&)
		{
			return {false,
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "GPU fences are unsupported by this backend provider.")};
		}

		/** Waits for the native GPU fence without idling unrelated queues. */
		virtual FArdaRHIStatus WaitGpuFence(const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "GPU fences are unsupported by this backend provider.");
		}

		/** Rearms a completed native GPU fence. */
		virtual FArdaRHIStatus ResetGpuFence(const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "GPU fences are unsupported by this backend provider.");
		}

		[[nodiscard]] virtual TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>> CreateCommandList(
		    EArdaRHIQueueType Queue,
		    bool bImmediate) = 0;
		[[nodiscard]] virtual TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList& CommandList,
		    EArdaRHIQueueType Queue) = 0;

		/** Adds a GPU-side dependency on a previously signaled submission. */
		virtual FArdaRHIStatus QueueWait(EArdaRHIQueueType WaitQueue,
		    EArdaRHIQueueType ExecutionQueue,
		    uint64_t Submission)
		{
			(void)WaitQueue;
			(void)ExecutionQueue;
			return WaitForSubmission(Submission);
		}

		/** Waits only for the requested submission when the API supports it. */
		virtual FArdaRHIStatus WaitForSubmission(uint64_t Submission)
		{
			(void)Submission;
			return WaitForIdle();
		}

		virtual FArdaRHIStatus WaitForIdle() = 0;

		/** Nonblocking completion check; tokens must originate from this provider device. */
		[[nodiscard]] virtual TArdaRHIResult<bool> PollSubmission(uint64_t Submission)
		{
			(void)Submission;
			return {false, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Submission polling is unavailable.")};
		}

		virtual void RunGarbageCollection() = 0;

		[[nodiscard]] virtual FArdaProviderLifetimeStats GetLifetimeStats() const noexcept
		{
			return {};
		}

		virtual void FlushPipelineCache() noexcept = 0;

	private:
		eastl::function<FArdaProviderObjectResult(const FArdaRHIBufferDesc&, EArdaRHIQueueType)> mBufferAllocator;
	};
}
