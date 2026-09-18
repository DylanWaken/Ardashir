/** Command recording contract implemented by backend providers. */
#pragma once

#include "RHI/Providers/ArdaProviderCommandTypes.h"
#include "RHI/Resources/ArdaRHIColor.h"
#include "RHI/Scheduling/ArdaRHIResourceCopies.h"
#include "RHI/Scheduling/ArdaRHITransitions.h"

namespace arda
{
	class IArdaProviderCommandList
	{
	public:
		/** Records a nonempty CUDA batch on one stream, with one graphics handoff/capture.
         * Bindings contain all views used by the ordered kernels and external calls. Each
         * operation's patches reference this table. Validate and prepare every operation
         * before enqueue/capture. External state must retire under its owning context;
         * providers without external-call support must reject those operations explicitly.
         */
		virtual FArdaRHIStatus DispatchCuda(const eastl::vector<FArdaProviderCudaBinding>&,
		    const eastl::vector<FArdaCudaKernel>&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Native CUDA recording is unavailable.");
		}

		/** Native recording state, queried before CUDA transitions are emitted. */
		[[nodiscard]] virtual bool IsOpen() const noexcept
		{
			return false;
		}

		virtual ~IArdaProviderCommandList() = default;
		virtual FArdaRHIStatus Open() = 0;
		virtual FArdaRHIStatus Close() = 0;
		virtual FArdaRHIStatus Reset() = 0;
		virtual FArdaRHIStatus WriteBuffer(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    const void* Data,
		    size_t Size,
		    uint64_t Offset) = 0;
		virtual FArdaRHIStatus CopyBuffer(const FArdaProviderObjectRef& Destination,
		    uint64_t DestinationOffset,
		    const FArdaProviderObjectRef& Source,
		    uint64_t SourceOffset,
		    uint64_t Size) = 0;
		virtual FArdaRHIStatus CopyTexture(const FArdaProviderObjectRef& Destination,
		    const FArdaRHITextureDesc& DestinationDesc,
		    const FArdaRHITextureSlice& DestinationSlice,
		    const FArdaProviderObjectRef& Source,
		    const FArdaRHITextureDesc& SourceDesc,
		    const FArdaRHITextureSlice& SourceSlice) = 0;

		/** Optional pitched GPU buffer-to-texture copy; unsupported providers reject explicitly. */
		virtual FArdaRHIStatus CopyBufferToTexture(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSlice&,
		    const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&,
		    const FArdaRHITextureBufferLayout&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "This provider does not support buffer-to-texture copies.");
		}

		/** Optional pitched GPU texture-to-buffer copy; unsupported providers reject explicitly. */
		virtual FArdaRHIStatus CopyTextureToBuffer(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&,
		    const FArdaRHITextureBufferLayout&,
		    const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaRHITextureSlice&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "This provider does not support texture-to-buffer copies.");
		}

		virtual FArdaRHIStatus ResolveTexture(const FArdaProviderObjectRef& Destination,
		    const FArdaRHITextureDesc& DestinationDesc,
		    const FArdaRHITextureSlice& DestinationSlice,
		    const FArdaProviderObjectRef& Source,
		    const FArdaRHITextureDesc& SourceDesc,
		    const FArdaRHITextureSlice& SourceSlice) = 0;
		virtual FArdaRHIStatus CopyTextureToStaging(const FArdaProviderObjectRef& Destination,
		    const FArdaRHIStagingTextureDesc& DestinationDesc,
		    const FArdaRHITextureSlice& DestinationSlice,
		    const FArdaProviderObjectRef& Source,
		    const FArdaRHITextureDesc& SourceDesc,
		    const FArdaRHITextureSlice& SourceSlice) = 0;
		virtual FArdaRHIStatus CopyTextureFromStaging(const FArdaProviderObjectRef& Destination,
		    const FArdaRHITextureDesc& DestinationDesc,
		    const FArdaRHITextureSlice& DestinationSlice,
		    const FArdaProviderObjectRef& Source,
		    const FArdaRHIStagingTextureDesc& SourceDesc,
		    const FArdaRHITextureSlice& SourceSlice) = 0;
		virtual FArdaRHIStatus ClearTexture(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range,
		    const FArdaRHIColor& Color) = 0;
		virtual FArdaRHIStatus ClearTextureUInt(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range,
		    uint32_t Value) = 0;
		virtual FArdaRHIStatus ClearBufferUInt(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    uint32_t Value) = 0;
		virtual FArdaRHIStatus ClearDepthStencilTexture(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range,
		    bool bClearDepth,
		    float Depth,
		    bool bClearStencil,
		    uint8_t Stencil) = 0;
		virtual FArdaRHIStatus SetTextureState(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range,
		    EArdaRHIResourceState State) = 0;
		virtual FArdaRHIStatus SetBufferState(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    EArdaRHIResourceState State) = 0;
		virtual FArdaRHIStatus TransitionTexture(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureTransitionDesc& Transition) = 0;
		virtual FArdaRHIStatus TransitionBuffer(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    const FArdaRHIBufferTransitionDesc& Transition) = 0;
		virtual void SetAutomaticBarriers(bool bEnabled) = 0;
		virtual FArdaRHIStatus BeginTrackingTextureState(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range,
		    EArdaRHIResourceState State) = 0;
		virtual FArdaRHIStatus BeginTrackingBufferState(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    EArdaRHIResourceState State) = 0;
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHINativeResourceState> QueryTextureState(
		    const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range) const = 0;
		[[nodiscard]] virtual TArdaRHIResult<FArdaRHINativeResourceState> QueryBufferState(
		    const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc) const = 0;
		virtual FArdaRHIStatus SetUAVBarriersForTexture(const FArdaProviderObjectRef& Texture, bool bEnabled) = 0;
		virtual FArdaRHIStatus SetUAVBarriersForBuffer(const FArdaProviderObjectRef& Buffer, bool bEnabled) = 0;
		virtual void CommitBarriers() = 0;
		virtual FArdaRHIStatus AliasingBarrier(const FArdaProviderObjectRef& ResourceBefore,
		    const FArdaProviderObjectRef& ResourceAfter) = 0;
		virtual FArdaRHIStatus SetGraphicsState(const FArdaProviderGraphicsState& State) = 0;
		virtual FArdaRHIStatus SetComputeState(const FArdaProviderComputeState& State) = 0;

		virtual FArdaRHIStatus SetMeshletState(const FArdaProviderMeshletState&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Mesh shaders are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus SetRayTracingState(const FArdaProviderRayTracingState&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Ray tracing is unsupported by this backend provider.");
		}

		virtual void SetPushConstants(const void* Data, size_t Size) = 0;
		virtual void Draw(const FArdaRHIDrawArguments& Arguments) = 0;
		virtual void DrawIndexed(const FArdaRHIDrawArguments& Arguments) = 0;
		virtual FArdaRHIStatus DrawIndirect(const FArdaProviderObjectRef& Arguments,
		    uint64_t Offset,
		    uint32_t DrawCount,
		    uint32_t Stride) = 0;
		virtual FArdaRHIStatus DrawIndexedIndirect(const FArdaProviderObjectRef& Arguments,
		    uint64_t Offset,
		    uint32_t DrawCount,
		    uint32_t Stride) = 0;
		virtual void Dispatch(uint32_t GroupsX, uint32_t GroupsY, uint32_t GroupsZ) = 0;
		virtual FArdaRHIStatus DispatchIndirect(const FArdaProviderObjectRef& Arguments, uint64_t Offset) = 0;

		virtual FArdaRHIStatus DispatchMesh(uint32_t, uint32_t, uint32_t)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Mesh shaders are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus DispatchRays(uint32_t, uint32_t, uint32_t)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Ray tracing is unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus DispatchRaysIndirect(const FArdaProviderObjectRef&, uint64_t)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Indirect ray dispatch is unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus DispatchWorkGraph(const FArdaProviderObjectRef&,
		    const void*,
		    uint32_t,
		    uint32_t,
		    const eastl::vector<FArdaProviderObjectRef>&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Work graphs are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus ClearSamplerFeedbackTexture(const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Sampler feedback is unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus DecodeSamplerFeedbackTexture(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaProviderObjectRef&,
		    EArdaRHIFormat)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Sampler feedback is unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus SetSamplerFeedbackTextureState(const FArdaProviderObjectRef&, EArdaRHIResourceState)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Sampler feedback is unsupported by this backend provider.");
		}

		[[nodiscard]] virtual TArdaRHIResult<FArdaRHINativeResourceState> QuerySamplerFeedbackTextureState(
		    const FArdaProviderObjectRef&) const
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Sampler feedback is unsupported by this backend provider.")};
		}

		virtual FArdaRHIStatus SetAccelStructState(const FArdaProviderObjectRef&, EArdaRHIResourceState)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Acceleration structures are unsupported by this backend provider.");
		}

		[[nodiscard]] virtual TArdaRHIResult<FArdaRHINativeResourceState> QueryAccelStructState(
		    const FArdaProviderObjectRef&) const
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Acceleration structures are unsupported by this backend provider.")};
		}

		virtual FArdaRHIStatus BuildBottomLevelAccelStruct(const FArdaProviderObjectRef&,
		    const eastl::vector<FArdaProviderRayTracingGeometry>&,
		    EArdaRHIAccelStructBuildFlags)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Acceleration structures are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus BuildTopLevelAccelStruct(const FArdaProviderObjectRef&,
		    const eastl::vector<FArdaProviderRayTracingInstance>&,
		    EArdaRHIAccelStructBuildFlags)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Acceleration structures are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus BuildTopLevelAccelStructFromBuffer(const FArdaProviderObjectRef&,
		    const FArdaProviderObjectRef&,
		    uint64_t,
		    size_t,
		    EArdaRHIAccelStructBuildFlags)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Acceleration structures are unsupported by this backend provider.");
		}

		/** Native same-kind acceleration-structure clone; facade validates source state and capacity. */
		virtual FArdaRHIStatus CopyAccelStruct(const FArdaProviderObjectRef&, const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Acceleration-structure cloning is unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus CompactAccelStruct(const FArdaProviderObjectRef&, const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Acceleration-structure compaction is unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus BuildOpacityMicromap(const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Opacity micromaps are unsupported by this backend provider.");
		}

		virtual FArdaRHIStatus CompactOpacityMicromap(const FArdaProviderObjectRef&, const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Opacity-micromap compaction is unsupported by this backend provider.");
		}

		[[nodiscard]] virtual TArdaRHIResult<FArdaRHINativeResourceState> QueryOpacityMicromapState(
		    const FArdaProviderObjectRef&) const
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			        "Opacity micromaps are unsupported by this backend provider.")};
		}

		/** Records the start timestamp for a provider timer query. */
		virtual FArdaRHIStatus BeginTimerQuery(const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Timer queries are unsupported by this backend provider.");
		}

		/** Records the end timestamp and result resolve for a timer query. */
		virtual FArdaRHIStatus EndTimerQuery(const FArdaProviderObjectRef&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Timer queries are unsupported by this backend provider.");
		}

		virtual void BeginMarker(const char* Name) = 0;
		virtual void EndMarker() = 0;
	};
}
