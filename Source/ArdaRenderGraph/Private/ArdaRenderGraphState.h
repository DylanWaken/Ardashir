#pragma once

#include "ArdaRenderGraphDefinitions.h"

namespace arda
{
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
	    EARDGPipeline Pipeline) noexcept
	{
		if (Pipeline != EARDGPipeline::AsyncCompute ||
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
