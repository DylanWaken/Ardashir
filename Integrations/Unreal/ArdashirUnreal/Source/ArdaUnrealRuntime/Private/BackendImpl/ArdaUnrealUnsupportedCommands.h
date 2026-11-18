#pragma once

#include "RHI/Providers/ArdaRHIProviderCommandList.h"

namespace arda
{
	/** Explicitly rejects provider operations outside the qualified Unreal compute subset. */
	class FArdaUnrealUnsupportedCommands : public IArdaProviderCommandList
	{
	public:
		FArdaRHIStatus Open() override
		{
			return Unsupported("Open");
		}

		FArdaRHIStatus Close() override
		{
			return Unsupported("Close");
		}

		FArdaRHIStatus Reset() override
		{
			return Unsupported("Reset");
		}

		FArdaRHIStatus WriteBuffer(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    const void* Data,
		    size_t Size,
		    uint64_t Offset) override
		{
			return Unsupported("WriteBuffer");
		}

		FArdaRHIStatus CopyBuffer(const FArdaProviderObjectRef& Destination,
		    uint64_t DestinationOffset,
		    const FArdaProviderObjectRef& Source,
		    uint64_t SourceOffset,
		    uint64_t Size) override
		{
			return Unsupported("CopyBuffer");
		}

		FArdaRHIStatus CopyTexture(const FArdaProviderObjectRef& Destination,
		    const FArdaRHITextureDesc& DestinationDesc,
		    const FArdaRHITextureSlice& DestinationSlice,
		    const FArdaProviderObjectRef& Source,
		    const FArdaRHITextureDesc& SourceDesc,
		    const FArdaRHITextureSlice& SourceSlice) override
		{
			return Unsupported("CopyTexture");
		}

		FArdaRHIStatus ResolveTexture(const FArdaProviderObjectRef& Destination,
		    const FArdaRHITextureDesc& DestinationDesc,
		    const FArdaRHITextureSlice& DestinationSlice,
		    const FArdaProviderObjectRef& Source,
		    const FArdaRHITextureDesc& SourceDesc,
		    const FArdaRHITextureSlice& SourceSlice) override
		{
			return Unsupported("ResolveTexture");
		}

		FArdaRHIStatus CopyTextureToStaging(const FArdaProviderObjectRef& Destination,
		    const FArdaRHIStagingTextureDesc& DestinationDesc,
		    const FArdaRHITextureSlice& DestinationSlice,
		    const FArdaProviderObjectRef& Source,
		    const FArdaRHITextureDesc& SourceDesc,
		    const FArdaRHITextureSlice& SourceSlice) override
		{
			return Unsupported("CopyTextureToStaging");
		}

		FArdaRHIStatus CopyTextureFromStaging(const FArdaProviderObjectRef& Destination,
		    const FArdaRHITextureDesc& DestinationDesc,
		    const FArdaRHITextureSlice& DestinationSlice,
		    const FArdaProviderObjectRef& Source,
		    const FArdaRHIStagingTextureDesc& SourceDesc,
		    const FArdaRHITextureSlice& SourceSlice) override
		{
			return Unsupported("CopyTextureFromStaging");
		}

		FArdaRHIStatus ClearTexture(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range,
		    const FArdaRHIColor& Color) override
		{
			return Unsupported("ClearTexture");
		}

		FArdaRHIStatus ClearTextureUInt(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range,
		    uint32_t Value) override
		{
			return Unsupported("ClearTextureUInt");
		}

		FArdaRHIStatus ClearBufferUInt(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    uint32_t Value) override
		{
			return Unsupported("ClearBufferUInt");
		}

		FArdaRHIStatus ClearDepthStencilTexture(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range,
		    bool bClearDepth,
		    float Depth,
		    bool bClearStencil,
		    uint8_t Stencil) override
		{
			return Unsupported("ClearDepthStencilTexture");
		}

		FArdaRHIStatus SetTextureState(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range,
		    EArdaRHIResourceState State) override
		{
			return Unsupported("SetTextureState");
		}

		FArdaRHIStatus SetBufferState(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    EArdaRHIResourceState State) override
		{
			return Unsupported("SetBufferState");
		}

		FArdaRHIStatus TransitionTexture(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureTransitionDesc& Transition) override
		{
			return Unsupported("TransitionTexture");
		}

		FArdaRHIStatus TransitionBuffer(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    const FArdaRHIBufferTransitionDesc& Transition) override
		{
			return Unsupported("TransitionBuffer");
		}

		void SetAutomaticBarriers(bool bEnabled) override
		{
			mStatus = Unsupported("SetAutomaticBarriers");
		}

		FArdaRHIStatus BeginTrackingTextureState(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range,
		    EArdaRHIResourceState State) override
		{
			return Unsupported("BeginTrackingTextureState");
		}

		FArdaRHIStatus BeginTrackingBufferState(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    EArdaRHIResourceState State) override
		{
			return Unsupported("BeginTrackingBufferState");
		}

		TArdaRHIResult<FArdaRHINativeResourceState> QueryTextureState(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaRHITextureSubresourceRange& Range) const override
		{
			return {{}, Unsupported("QueryTextureState")};
		}

		TArdaRHIResult<FArdaRHINativeResourceState> QueryBufferState(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc) const override
		{
			return {{}, Unsupported("QueryBufferState")};
		}

		FArdaRHIStatus SetUAVBarriersForTexture(const FArdaProviderObjectRef& Texture, bool bEnabled) override
		{
			return Unsupported("SetUAVBarriersForTexture");
		}

		FArdaRHIStatus SetUAVBarriersForBuffer(const FArdaProviderObjectRef& Buffer, bool bEnabled) override
		{
			return Unsupported("SetUAVBarriersForBuffer");
		}

		void CommitBarriers() override
		{
			mStatus = Unsupported("CommitBarriers");
		}

		FArdaRHIStatus AliasingBarrier(const FArdaProviderObjectRef& ResourceBefore,
		    const FArdaProviderObjectRef& ResourceAfter) override
		{
			return Unsupported("AliasingBarrier");
		}

		FArdaRHIStatus SetGraphicsState(const FArdaProviderGraphicsState& State) override
		{
			return Unsupported("SetGraphicsState");
		}

		FArdaRHIStatus SetComputeState(const FArdaProviderComputeState& State) override
		{
			return Unsupported("SetComputeState");
		}

		void SetPushConstants(const void* Data, size_t Size) override
		{
			mStatus = Unsupported("SetPushConstants");
		}

		void Draw(const FArdaRHIDrawArguments& Arguments) override
		{
			mStatus = Unsupported("Draw");
		}

		void DrawIndexed(const FArdaRHIDrawArguments& Arguments) override
		{
			mStatus = Unsupported("DrawIndexed");
		}

		FArdaRHIStatus DrawIndirect(const FArdaProviderObjectRef& Arguments,
		    uint64_t Offset,
		    uint32_t DrawCount,
		    uint32_t Stride) override
		{
			return Unsupported("DrawIndirect");
		}

		FArdaRHIStatus DrawIndexedIndirect(const FArdaProviderObjectRef& Arguments,
		    uint64_t Offset,
		    uint32_t DrawCount,
		    uint32_t Stride) override
		{
			return Unsupported("DrawIndexedIndirect");
		}

		void Dispatch(uint32_t GroupsX, uint32_t GroupsY, uint32_t GroupsZ) override
		{
			mStatus = Unsupported("Dispatch");
		}

		FArdaRHIStatus DispatchIndirect(const FArdaProviderObjectRef& Arguments, uint64_t Offset) override
		{
			return Unsupported("DispatchIndirect");
		}

		void BeginMarker(const char* Name) override
		{
			mStatus = Unsupported("BeginMarker");
		}

		void EndMarker() override
		{
			mStatus = Unsupported("EndMarker");
		}

	protected:
		static FArdaRHIStatus Unsupported(const char* Operation)
		{
			eastl::string Message = "unreal-rhi does not support ";
			Message += Operation;
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Message.c_str());
		}

		FArdaRHIStatus mStatus;
	};
}
