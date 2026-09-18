/** @file ArdaRHICommandList.h
 * Command recording and submission-facing copy callbacks.
 */
#pragma once

#include "RHI/Resources/ArdaRHIResources.h"
#include "RHI/CUDA/ArdaRHICuda.h"
#include <EASTL/functional.h>

namespace arda
{
	/** Result returned by a device-to-host buffer copy. */
	using FArdaRHIBufferReadbackResult = TArdaRHIResult<eastl::vector<uint8_t>>;

	/** Called after an asynchronous host-to-device copy reaches the GPU. */
	using FArdaRHIHostToDeviceCopyCallback = eastl::function<void(FArdaRHIStatus)>;

	/** Called with owned bytes after an asynchronous device-to-host copy. */
	using FArdaRHIDeviceToHostCopyCallback = eastl::function<void(FArdaRHIBufferReadbackResult)>;

	/** Interface for command list. */
	class IArdaRHICommandList : public virtual IArdaRHIResource
	{
	public:
		/**
         * Records one precompiled CUDA kernel on an open list; ExecuteCommandList submits the work.
         * Bindings must belong to this device and already have qualified CUDA representations.
         * Providers insert memory dependencies and retain bindings/native entries until completion.
         * D3D12 CiG accepts graphics lists; Vulkan and ContextSwitch accept graphics/compute
         * lists. Copy lists are rejected. ContextSwitch and Vulkan CiG record deferred segments
         * joined by GPU fence/semaphore handoffs at submission, without per-kernel CPU waits.
         * Final graphics completion proves CUDA completion. CUDA lists are single-use until reset.
         * Execution failures are not retried as graphics work.
         */
		virtual FArdaRHIStatus DispatchCuda(const FArdaCudaDispatch&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "CUDA recording is unavailable.");
		}

		/** Records an ordered sequence of single-operation dispatches as one native CUDA batch.
         * All steps are validated before capture/launch. The union of their resources is
         * acquired once and retained through completion, including intermediate buffers.
         * Kernels execute on one stream without intervening graphics work or CPU waits.
         * An empty sequence is a no-op on an open supported list. Other restrictions match DispatchCuda.
         */
		virtual FArdaRHIStatus DispatchCudaSequence(const eastl::vector<FArdaCudaDispatch>&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "CUDA sequence recording is unavailable.");
		}

		/**
         * Device retained by this command list, including after process-wide backend shutdown.
         * @return A borrowed pointer valid for the lifetime of this command list.
         */
		[[nodiscard]] virtual IArdaRHIDevice* GetDevice() const noexcept = 0;

		/**
         * Returns the queue type.
         * @return The requested value.
         */
		[[nodiscard]] virtual EArdaRHIQueueType GetQueueType() const noexcept = 0;

		/**
         * Begins a new recording generation and clears prior binding state and latched errors on success.
         * Pending native recording storage remains retained until its submission retires.
         * @return The native open status; a failed open does not clear the facade's prior error.
         * @ownership The command list retains its device and owns the new recording; earlier submitted storage has independent retirement ownership.
         * @errors Native failure leaves this operation unsuccessful. Only a successful Open or Reset clears a recording error.
         * @threading Externally synchronize access to this command list.
         */
		virtual FArdaRHIStatus Open() = 0;

		/**
         * Finalizes an open recording and returns any retained recording error.
         * A successful close permits submission but proves neither GPU execution nor completion.
         * @return The first latched recording error, or the native close status.
         * @ownership Closing preserves objects referenced by the recording; submission establishes their pending lifetime.
         * @errors Closing a non-open list returns InvalidState. Invalid void work and native binding failures prevent successful submission until Open or Reset succeeds.
         * @threading Externally synchronize access to this command list.
         */
		virtual FArdaRHIStatus Close() = 0;

		/**
         * Discards the previous recording and opens a clean recording generation on success.
         * All pipeline state must be rebound. Pending native storage is retained independently instead of reset while the GPU can use it.
         * @return The native reset status; failure does not clear the prior facade error.
         * @ownership The command list owns the fresh recording; submitted generations retain their own native storage and resources.
         * @errors Only successful Reset or Open clears latched recording failures.
         * @threading Externally synchronize access to this command list.
         */
		virtual FArdaRHIStatus Reset() = 0;

		/**
         * Performs the write buffer operation.
         * @param Buffer The buffer.
         * @param Data The data.
         * @param Size The size.
         * @param Offset The offset.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus WriteBuffer(IArdaRHIBuffer& Buffer,
		    const void* Data,
		    size_t Size,
		    uint64_t Offset = 0) = 0;

		/**
         * Records a host-to-device copy and makes submission wait for its GPU
         * completion. The source bytes are copied while this method executes.
         */
		virtual FArdaRHIStatus CopyBufferHostToDevice(IArdaRHIBuffer& Destination,
		    const void* SourceData,
		    size_t Size,
		    uint64_t DestinationOffset = 0) = 0;

		/**
         * Records a host-to-device copy whose completion callback is invoked
         * asynchronously after the submitted copy reaches the GPU.
         */
		virtual FArdaRHIStatus CopyBufferHostToDeviceAsync(IArdaRHIBuffer& Destination,
		    const void* SourceData,
		    size_t Size,
		    FArdaRHIHostToDeviceCopyCallback Completion,
		    uint64_t DestinationOffset = 0) = 0;

		/**
         * Records a device-to-host copy. Submission waits for completion and
         * fills Output before ExecuteCommandList returns.
         */
		virtual FArdaRHIStatus CopyBufferDeviceToHost(IArdaRHIBuffer& Source,
		    eastl::vector<uint8_t>& Output,
		    uint64_t SourceOffset = 0,
		    uint64_t Size = ArdaRHIWholeBuffer) = 0;

		/**
         * Records a device-to-host copy and invokes Completion asynchronously
         * with owned readback bytes after the submitted copy completes.
         */
		virtual FArdaRHIStatus CopyBufferDeviceToHostAsync(IArdaRHIBuffer& Source,
		    FArdaRHIDeviceToHostCopyCallback Completion,
		    uint64_t SourceOffset = 0,
		    uint64_t Size = ArdaRHIWholeBuffer) = 0;

		/**
         * Performs the copy buffer operation.
         * @param Destination The destination.
         * @param DestinationOffset The destination offset.
         * @param Source The source.
         * @param SourceOffset The source offset.
         * @param Size The size.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus CopyBuffer(IArdaRHIBuffer& Destination,
		    uint64_t DestinationOffset,
		    IArdaRHIBuffer& Source,
		    uint64_t SourceOffset,
		    uint64_t Size) = 0;

		/** Copies a texture region between matching subresources. */
		virtual FArdaRHIStatus CopyTexture(IArdaRHITexture& Destination,
		    const FArdaRHITextureSlice& DestinationSlice,
		    IArdaRHITexture& Source,
		    const FArdaRHITextureSlice& SourceSlice) = 0;

		/** Copies a pitched buffer region into one texture mip/layer or 3D region.
         * Supports single-sample, uncompressed color formats. Automatic barriers temporarily
         * transition both resources and restore their states; otherwise callers supply copy states.
         * Source must not be a CPU-read buffer. Both resources are retained through submission.
         */
		virtual FArdaRHIStatus CopyBufferToTexture(IArdaRHITexture& Destination,
		    const FArdaRHITextureSlice& DestinationSlice,
		    IArdaRHIBuffer& Source,
		    const FArdaRHITextureBufferLayout& SourceLayout) = 0;

		/** Copies one texture mip/layer or 3D region into a pitched buffer without CPU readback.
         * Supports single-sample, uncompressed color formats. Automatic barriers temporarily
         * transition both resources and restore their states; otherwise callers supply copy states.
         * Destination must not be a CPU-write buffer. Both resources survive GPU completion.
         */
		virtual FArdaRHIStatus CopyTextureToBuffer(IArdaRHIBuffer& Destination,
		    const FArdaRHITextureBufferLayout& DestinationLayout,
		    IArdaRHITexture& Source,
		    const FArdaRHITextureSlice& SourceSlice) = 0;

		/** Resolves one multisampled texture subresource into a single-sample texture. */
		virtual FArdaRHIStatus ResolveTexture(IArdaRHITexture& Destination,
		    const FArdaRHITextureSlice& DestinationSlice,
		    IArdaRHITexture& Source,
		    const FArdaRHITextureSlice& SourceSlice) = 0;

		/**
         * Performs the copy texture to staging operation.
         * @param Destination The destination.
         * @param DestinationSlice The destination slice.
         * @param Source The source.
         * @param SourceSlice The source slice.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus CopyTextureToStaging(IArdaRHIStagingTexture& Destination,
		    const FArdaRHITextureSlice& DestinationSlice,
		    IArdaRHITexture& Source,
		    const FArdaRHITextureSlice& SourceSlice) = 0;

		/**
         * Performs the copy texture from staging operation.
         * @param Destination The destination.
         * @param DestinationSlice The destination slice.
         * @param Source The source.
         * @param SourceSlice The source slice.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus CopyTextureFromStaging(IArdaRHITexture& Destination,
		    const FArdaRHITextureSlice& DestinationSlice,
		    IArdaRHIStagingTexture& Source,
		    const FArdaRHITextureSlice& SourceSlice) = 0;

		/**
         * Performs the clear texture operation.
         * @param Texture The texture.
         * @param Range The range.
         * @param Color The color.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus ClearTexture(IArdaRHITexture& Texture,
		    const FArdaRHITextureSubresourceRange& Range,
		    const FArdaRHIColor& Color) = 0;

		/**
         * Performs the set texture state operation.
         * @param Texture The texture.
         * @param Range The range.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus SetTextureState(IArdaRHITexture& Texture,
		    const FArdaRHITextureSubresourceRange& Range,
		    EArdaRHIResourceState State) = 0;

		/**
         * Performs the set buffer state operation.
         * @param Buffer The buffer.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus SetBufferState(IArdaRHIBuffer& Buffer, EArdaRHIResourceState State) = 0;

		/** Records an explicit before/after texture transition. */
		virtual FArdaRHIStatus TransitionTexture(IArdaRHITexture& Texture,
		    const FArdaRHITextureTransitionDesc& Transition) = 0;

		/** Records an explicit before/after buffer transition. */
		virtual FArdaRHIStatus TransitionBuffer(IArdaRHIBuffer& Buffer,
		    const FArdaRHIBufferTransitionDesc& Transition) = 0;

		/**
         * Performs the set accel struct state operation.
         * @param AccelStruct The accel struct.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus SetAccelStructState(IArdaRHIAccelStruct& AccelStruct, EArdaRHIResourceState State) = 0;

		/** Observes facade/backend/native acceleration-structure state. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryAccelStructState(
		    IArdaRHIAccelStruct& AccelStruct) const = 0;

		/**
         * Performs the set automatic barriers operation.
         * @param bEnabled The b enabled.
         */
		virtual void SetAutomaticBarriers(bool bEnabled) = 0;

		/**
         * Performs the begin tracking texture state operation.
         * @param Texture The texture.
         * @param Range The range.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus BeginTrackingTextureState(IArdaRHITexture& Texture,
		    const FArdaRHITextureSubresourceRange& Range,
		    EArdaRHIResourceState State) = 0;

		/**
         * Performs the begin tracking buffer state operation.
         * @param Buffer The buffer.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus BeginTrackingBufferState(IArdaRHIBuffer& Buffer, EArdaRHIResourceState State) = 0;

		/**
         * Observes independently tracked facade, backend, and native texture
         * state. The resolved range must contain a uniform state.
         * @param Texture The texture.
         * @param Range The texture subresources to observe.
         * @return The state snapshot or a diagnostic status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryTextureState(IArdaRHITexture& Texture,
		    const FArdaRHITextureSubresourceRange& Range) const = 0;

		/**
         * Observes independently tracked facade, backend, and native buffer
         * state.
         * @param Buffer The buffer.
         * @return The state snapshot or a diagnostic status.
         */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryBufferState(
		    IArdaRHIBuffer& Buffer) const = 0;

		/** Returns facade, command-tracker, and native state for sampler feedback. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIResourceStateSnapshot> QuerySamplerFeedbackTextureState(
		    IArdaRHISamplerFeedbackTexture& Texture) const = 0;

		/**
         * Validates the observed texture state against an expected state.
         * @param Texture The texture.
         * @param Range The texture subresources to validate.
         * @param ExpectedState The required state.
         * @return Success when every tracked layer agrees with ExpectedState.
         */
		[[nodiscard]] FArdaRHIStatus AssertTextureState(IArdaRHITexture& Texture,
		    const FArdaRHITextureSubresourceRange& Range,
		    EArdaRHIResourceState ExpectedState) const
		{
			const auto Snapshot = QueryTextureState(Texture, Range);
			if (!Snapshot)
			{
				return Snapshot.mStatus;
			}
			if (!Snapshot.mValue.IsConsistent() || Snapshot.mValue.mFacadeState != ExpectedState)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "Texture state does not match the expected facade/backend/native state.");
			}
			return {};
		}

		/**
         * Validates the observed buffer state against an expected state.
         * @param Buffer The buffer.
         * @param ExpectedState The required state.
         * @return Success when every tracked layer agrees with ExpectedState.
         */
		[[nodiscard]] FArdaRHIStatus AssertBufferState(IArdaRHIBuffer& Buffer,
		    EArdaRHIResourceState ExpectedState) const
		{
			const auto Snapshot = QueryBufferState(Buffer);
			if (!Snapshot)
			{
				return Snapshot.mStatus;
			}
			if (!Snapshot.mValue.IsConsistent() || Snapshot.mValue.mFacadeState != ExpectedState)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "Buffer state does not match the expected facade/backend/native state.");
			}
			return {};
		}

		/**
         * Performs the set UAVbarriers for texture operation.
         * @param Texture The texture.
         * @param bEnabled The b enabled.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus SetUAVBarriersForTexture(IArdaRHITexture& Texture, bool bEnabled) = 0;

		/**
         * Performs the set UAVbarriers for buffer operation.
         * @param Buffer The buffer.
         * @param bEnabled The b enabled.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus SetUAVBarriersForBuffer(IArdaRHIBuffer& Buffer, bool bEnabled) = 0;

		/** Performs the commit barriers operation. */
		virtual void CommitBarriers() = 0;

		/** Declares that memory is changing ownership between aliased resources. */
		virtual FArdaRHIStatus AliasingBarrier(IArdaRHIResource* ResourceBefore, IArdaRHIResource* ResourceAfter) = 0;

		/**
         * Performs the clear texture uint operation.
         * @param Texture The texture.
         * @param Range The range.
         * @param Value The value.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus ClearTextureUInt(IArdaRHITexture& Texture,
		    const FArdaRHITextureSubresourceRange& Range,
		    uint32_t Value) = 0;

		/**
         * Performs the clear depth stencil texture operation.
         * @param Texture The texture.
         * @param Range The range.
         * @param bClearDepth The b clear depth.
         * @param Depth The depth.
         * @param bClearStencil The b clear stencil.
         * @param Stencil The stencil.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus ClearDepthStencilTexture(IArdaRHITexture& Texture,
		    const FArdaRHITextureSubresourceRange& Range,
		    bool bClearDepth,
		    float Depth,
		    bool bClearStencil,
		    uint8_t Stencil) = 0;

		/**
         * Performs the clear buffer uint operation.
         * @param Buffer The buffer.
         * @param Value The value.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus ClearBufferUInt(IArdaRHIBuffer& Buffer, uint32_t Value) = 0;

		/**
         * Binds graphics pipeline, framebuffer, descriptors, vertex/index streams and raster regions on an open graphics list.
         * Formats, sample counts, selected attachment ranges, viewport/scissor limits and vertex/index usage, alignment and bounds must agree.
         * Descriptor-bearing layouts require one matching binding set; sets are normalized into declared layout order. Omitted push-only sets are reused within the recording.
         * @param State Complete graphics state for subsequent draws.
         * @return Success after the native bind succeeds, otherwise its admission or native failure status.
         * @ownership State handles and immutable descriptor generations are retained for the recording and accepted GPU work.
         * @errors Facade rejection preserves the previous binding. Native bind failure latches a failed recording because partial driver state cannot be rolled back; reset before submission.
         * @threading Externally synchronize command-list recording and mutable descriptor-table updates.
         */
		virtual FArdaRHIStatus SetGraphicsState(const FArdaRHIGraphicsState& State) = 0;

		/**
         * Binds a compute pipeline and its complete descriptor state on an open graphics or compute list.
         * Each descriptor-bearing layout requires one matching set; declaration order controls native binding. Omitted push-only sets are reused within the recording.
         * @param State Compute pipeline and binding sets for subsequent dispatches.
         * @return Success only after native binding succeeds.
         * @ownership Pipeline handles, binding resources and immutable descriptor generations are retained for recording and accepted GPU work.
         * @errors Facade rejection preserves the previous binding. Native failure latches the recording as failed. A later bind cannot repair an already latched error; successful Open or Reset is required.
         * @threading Externally synchronize command-list recording and mutable descriptor-table updates.
         */
		virtual FArdaRHIStatus SetComputeState(const FArdaRHIComputeState& State) = 0;

		/**
         * Binds a mesh pipeline, compatible framebuffer, raster regions and required descriptor sets on an open graphics list.
         * @param State Complete mesh state for subsequent mesh dispatches.
         * @return The facade admission or native binding status.
         * @ownership Referenced objects and captured descriptor generations remain retained for recording and accepted GPU work.
         * @errors Unsupported stages or incompatible state are rejected before native work. Native bind failure latches the recording until successful Open or Reset.
         * @threading Externally synchronize command-list recording.
         */
		virtual FArdaRHIStatus SetMeshletState(const FArdaRHIMeshletState& State) = 0;

		/**
         * Binds a committed shader-table generation and its required global descriptor sets on an open graphics or compute list.
         * @param State Shader table and global bindings for subsequent ray dispatches.
         * @return The facade admission or native binding status.
         * @ownership The bound immutable shader-table and descriptor generations remain retained even if the mutable table is later recommitted.
         * @errors Missing or incompatible state is rejected before binding; native binding failure latches the recording until successful Open or Reset.
         * @threading Externally synchronize command-list recording and shader-table mutation.
         */
		virtual FArdaRHIStatus SetRayTracingState(const FArdaRHIRayTracingState& State) = 0;

		/** Broadcasts bytes to the bound pipeline's push blocks. Data must be nonnull and Size must be
		 * positive, divisible by four and no larger than any bound push block. Invalid updates record
		 * no native command and cause Close() and submission to return InvalidArgument until a
		 * successful Reset()/Open(). Previous valid constants remain unchanged.
		 * These checks apply independently of native GPU validation.
		 * @param Data Bytes broadcast identically to every push block in the bound pipeline.
		 * @param Size Number of bytes; positive, divisible by four and bounded by the smallest block.
		 * @return No immediate result; inspect Close() before submitting the recording.
		 * @ownership Data is borrowed for this call and copied into the recording; the caller retains its storage.
		 * @errors InvalidArgument is retained by Close() and submission for null data, invalid size or no bound block.
		 * Only a successful Reset()/Open() clears the error. Distinct values cannot be targeted to separate blocks.
		 * @threading Externally synchronize command-list recording and do not update a closed or submitted recording.
		 */
		virtual void SetPushConstants(const void* Data, size_t Size) = 0;

		/**
         * Draws with the most recently bound graphics pipeline in this recording generation.
         * Requires an open graphics list. Known vertex/instance fetch ranges are checked before recording; zero vertices or instances perform no work after validation.
         * @param Args Vertex and instance counts plus start offsets.
         * @return No immediate result; inspect Close before submitting the recording.
         * @ownership Previously bound resources remain retained by the recording; this call transfers no caller storage.
         * @errors Invalid work records no native draw and latches an error returned by Close and submission until Open or Reset succeeds.
         * @threading Externally synchronize command-list recording.
         */
		virtual void Draw(const FArdaRHIDrawArguments& Arguments) = 0;

		/**
         * Draws indexed geometry with the most recently bound graphics pipeline on an open graphics list.
         * Requires an aligned R16UInt or R32UInt index buffer and an in-bounds index range. GPU index values and base-vertex accesses remain the caller's responsibility.
         * Zero indices or instances perform no work after validation.
         * @param Args Index and instance counts plus index, base-vertex and instance offsets.
         * @return No immediate result; inspect Close before submitting the recording.
         * @ownership Bound index/vertex resources remain retained by the recording; this call transfers no caller storage.
         * @errors Invalid work records no native draw and latches an error returned by Close and submission until Open or Reset succeeds.
         * @threading Externally synchronize command-list recording.
         */
		virtual void DrawIndexed(const FArdaRHIDrawArguments& Arguments) = 0;

		/** Executes non-indexed draw arguments with a bound graphics pipeline. GPU-written firstInstance
         * must be zero unless capabilities.mbIndirectFirstInstance is true. GPU argument values and
         * their resulting fetch ranges are the caller's responsibility; CPU offsets/strides are checked. */
		virtual FArdaRHIStatus DrawIndirect(IArdaRHIBuffer& Arguments,
		    uint64_t Offset = 0,
		    uint32_t DrawCount = 1,
		    uint32_t Stride = 0) = 0;

		/** Executes indexed draw arguments from a GPU buffer. */
		virtual FArdaRHIStatus DrawIndexedIndirect(IArdaRHIBuffer& Arguments,
		    uint64_t Offset = 0,
		    uint32_t DrawCount = 1,
		    uint32_t Stride = 0) = 0;

		/**
         * Dispatches the most recently bound compute pipeline on an open graphics or compute list.
         * Group counts must fit the reported device limits. Any zero axis performs no work after state and limit validation.
         * @param GroupsX Number of workgroups on the X axis.
         * @param GroupsY Number of workgroups on the Y axis.
         * @param GroupsZ Number of workgroups on the Z axis.
         * @return No immediate result; inspect Close before submitting the recording.
         * @ownership Previously bound resources remain retained by the recording; no caller storage is transferred.
         * @errors Invalid work records no native dispatch and latches an error returned by Close and submission until Open or Reset succeeds. Later binds do not repair it.
         * @threading Externally synchronize command-list recording.
         */
		virtual void Dispatch(uint32_t GroupsX, uint32_t GroupsY = 1, uint32_t GroupsZ = 1) = 0;

		/** Executes compute dispatch dimensions from a GPU buffer. */
		virtual FArdaRHIStatus DispatchIndirect(IArdaRHIBuffer& Arguments, uint64_t Offset = 0) = 0;

		/**
         * Performs the dispatch mesh operation.
         * @param GroupsX The groups x.
         * @param GroupsY The groups y.
         * @param GroupsZ The groups z.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus DispatchMesh(uint32_t GroupsX, uint32_t GroupsY = 1, uint32_t GroupsZ = 1) = 0;

		/**
         * Performs the dispatch rays operation.
         * @param Width The width.
         * @param Height The height.
         * @param Depth The depth.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus DispatchRays(uint32_t Width, uint32_t Height = 1, uint32_t Depth = 1) = 0;

		/** Executes ray-dispatch dimensions from a GPU argument buffer. */
		virtual FArdaRHIStatus DispatchRaysIndirect(IArdaRHIBuffer& Arguments, uint64_t Offset = 0) = 0;

		/**
         * Performs the build bottom level accel struct operation.
         * @param AccelStruct The accel struct.
         * @param Geometries The geometries.
         * @param Flags The flags.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus BuildBottomLevelAccelStruct(IArdaRHIAccelStruct& AccelStruct,
		    const eastl::vector<FArdaRHIRayTracingGeometryDesc>& Geometries,
		    EArdaRHIAccelStructBuildFlags Flags) = 0;

		/**
         * Performs the build top level accel struct operation.
         * @param AccelStruct The accel struct.
         * @param Instances The instances.
         * @param Flags The flags.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus BuildTopLevelAccelStruct(IArdaRHIAccelStruct& AccelStruct,
		    const eastl::vector<FArdaRHIRayTracingInstanceDesc>& Instances,
		    EArdaRHIAccelStructBuildFlags Flags) = 0;

		/**
         * Builds a TLAS from native 64-byte instance records in GPU memory.
         * The count is supplied by the CPU; this is not an indirect-count build.
         * Instance addresses must refer to live BLAS resources through completion.
         * @param AccelStruct The accel struct.
         * @param InstanceBuffer The instance buffer.
         * @param Offset The offset.
         * @param InstanceCount The instance count.
         * @param Flags The flags.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus BuildTopLevelAccelStructFromBuffer(IArdaRHIAccelStruct& AccelStruct,
		    IArdaRHIBuffer& InstanceBuffer,
		    uint64_t Offset,
		    size_t InstanceCount,
		    EArdaRHIAccelStructBuildFlags Flags) = 0;

		/** Clones a built BLAS or TLAS into a distinct same-kind destination with matching build flags
		 * and at least the source result allocation size. Preserves built/updated/compacted state;
		 * referenced BLAS addresses are unchanged. Requires a graphics or compute command list.
		 */
		virtual FArdaRHIStatus CopyAccelStruct(IArdaRHIAccelStruct&, IArdaRHIAccelStruct&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Acceleration-structure cloning is unsupported by this command list.");
		}

		/** Copies a built acceleration structure into a compact-size destination. */
		virtual FArdaRHIStatus CompactAccelStruct(IArdaRHIAccelStruct& Destination, IArdaRHIAccelStruct& Source) = 0;

		/** Dispatches a work graph with CPU entry records. */
		virtual FArdaRHIStatus DispatchWorkGraph(IArdaRHIWorkGraphPipeline&,
		    const void*,
		    uint32_t,
		    uint32_t,
		    const eastl::vector<FArdaRHIBindingSetRef>& = {})
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Work graphs are unsupported by this command-list implementation.");
		}

		/**
         * Dispatches every enabled record in a shader bundle. Mesh records
         * inherit the framebuffer, viewports and scissors from SetMeshletState.
         */
		virtual FArdaRHIStatus DispatchShaderBundle(IArdaRHIShaderBundle&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Shader bundles are unsupported by this command-list implementation.");
		}

		/**
         * Performs the build opacity micromap operation.
         * @param Micromap The micromap.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus BuildOpacityMicromap(IArdaRHIOpacityMicromap& Micromap) = 0;

		/** Copies a built opacity micromap into a compact-size destination. */
		virtual FArdaRHIStatus CompactOpacityMicromap(IArdaRHIOpacityMicromap&, IArdaRHIOpacityMicromap&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Opacity-micromap compaction is unsupported by this command-list implementation.");
		}

		/** Observes facade/backend/native opacity-micromap state. */
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryOpacityMicromapState(
		    IArdaRHIOpacityMicromap&) const
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Opacity-micromap state queries are unsupported by this command-list implementation.")};
		}

		/**
         * Performs the clear sampler feedback texture operation.
         * @param Texture The texture.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus ClearSamplerFeedbackTexture(IArdaRHISamplerFeedbackTexture& Texture) = 0;

		/**
         * Performs the decode sampler feedback texture operation.
         * @param Destination The destination.
         * @param Texture The texture.
         * @param Format The format.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus DecodeSamplerFeedbackTexture(IArdaRHITexture& Destination,
		    IArdaRHISamplerFeedbackTexture& Texture,
		    EArdaRHIFormat Format) = 0;

		/**
         * Performs the set sampler feedback texture state operation.
         * @param Texture The texture.
         * @param State The state.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus SetSamplerFeedbackTextureState(IArdaRHISamplerFeedbackTexture& Texture,
		    EArdaRHIResourceState State) = 0;

		/**
         * Performs the begin timer query operation.
         * @param Query The query.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus BeginTimerQuery(IArdaRHITimerQuery& Query) = 0;

		/**
         * Performs the end timer query operation.
         * @param Query The query.
         * @return A status describing whether the operation succeeded.
         */
		virtual FArdaRHIStatus EndTimerQuery(IArdaRHITimerQuery& Query) = 0;

		/**
         * Performs the begin marker operation.
         * @param Name The name.
         */
		virtual void BeginMarker(const char* Name) = 0;

		/** Performs the end marker operation. */
		virtual void EndMarker() = 0;
	};
}
