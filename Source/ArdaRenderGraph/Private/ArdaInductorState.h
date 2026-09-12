#pragma once

#include "ArdaInductorCommandProgram.h"

namespace arda
{
	[[nodiscard]] inline size_t GetInductorTextureStateCount(const FArdaRHITextureDesc& Desc) noexcept
	{
		return static_cast<size_t>(Desc.mMipLevels) * Desc.mArraySize * GetArdaRHIFormatPlaneCount(Desc.mFormat);
	}

	/** Tracks depth/stencil planes independently, just like mip levels and array slices. */
	template <class Visitor>
	void VisitInductorTextureCells(const FArdaRHITextureDesc& Desc,
	    const FArdaRHITextureSubresourceRange& Range,
	    Visitor&& Visit)
	{
		const auto Resolved = Range.Resolve(Desc);
		for (uint32_t Plane = Resolved.mBasePlane; Plane < Resolved.mBasePlane + Resolved.mPlaneCount; ++Plane)
		{
			for (uint32_t Slice = Resolved.mBaseArraySlice;
			    Slice < Resolved.mBaseArraySlice + Resolved.mArraySliceCount;
			    ++Slice)
			{
				for (uint32_t Mip = Resolved.mBaseMipLevel; Mip < Resolved.mBaseMipLevel + Resolved.mMipLevelCount;
				    ++Mip)
				{
					const size_t Index = (static_cast<size_t>(Plane) * Desc.mArraySize + Slice) * Desc.mMipLevels + Mip;
					Visit(FArdaRHITextureSubresourceRange{Mip, 1, Slice, 1, Plane, 1}, Index);
				}
			}
		}
	}

	// One state classification contract for setup, compilation, and execution.
	inline constexpr uint32_t WriteMask = static_cast<uint32_t>(arda::EArdaRHIResourceState::UnorderedAccess) |
	    static_cast<uint32_t>(arda::EArdaRHIResourceState::RenderTarget) |
	    static_cast<uint32_t>(arda::EArdaRHIResourceState::DepthWrite) |
	    static_cast<uint32_t>(arda::EArdaRHIResourceState::CopyDest) |
	    static_cast<uint32_t>(arda::EArdaRHIResourceState::ResolveDest) |
	    static_cast<uint32_t>(arda::EArdaRHIResourceState::AccelStructWrite);

	inline constexpr uint32_t CopyMask = static_cast<uint32_t>(arda::EArdaRHIResourceState::CopySource) |
	    static_cast<uint32_t>(arda::EArdaRHIResourceState::CopyDest);

	inline constexpr uint32_t GraphicsOnlyMask = static_cast<uint32_t>(arda::EArdaRHIResourceState::RenderTarget) |
	    static_cast<uint32_t>(arda::EArdaRHIResourceState::DepthWrite) |
	    static_cast<uint32_t>(arda::EArdaRHIResourceState::DepthRead) |
	    static_cast<uint32_t>(arda::EArdaRHIResourceState::Present);

	[[nodiscard]] inline bool IsWriteState(arda::EArdaRHIResourceState State) noexcept
	{
		return (static_cast<uint32_t>(State) & WriteMask) != 0;
	}

	[[nodiscard]] inline bool IsUAVState(arda::EArdaRHIResourceState State) noexcept
	{
		return (State & arda::EArdaRHIResourceState::UnorderedAccess) != arda::EArdaRHIResourceState::Unknown;
	}

	[[nodiscard]] inline arda::EArdaRHIResourceState NormalizeStateForPipeline(arda::EArdaRHIResourceState State,
	    EArdaRHIQueueType Pipeline) noexcept
	{
		if (Pipeline != EArdaRHIQueueType::Compute ||
		    (State & arda::EArdaRHIResourceState::PixelShaderResource) == arda::EArdaRHIResourceState::Unknown ||
		    (State & arda::EArdaRHIResourceState::NonPixelShaderResource) == arda::EArdaRHIResourceState::Unknown)
		{
			return State;
		}

		return static_cast<arda::EArdaRHIResourceState>(
		    static_cast<uint32_t>(State) & ~static_cast<uint32_t>(arda::EArdaRHIResourceState::PixelShaderResource));
	}

	[[nodiscard]] inline arda::EArdaRHIResourceState NormalizeInitialState(arda::EArdaRHIResourceState State) noexcept
	{
		return State == arda::EArdaRHIResourceState::Unknown ? arda::EArdaRHIResourceState::Common : State;
	}
}
