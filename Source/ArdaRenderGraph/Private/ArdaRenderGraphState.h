#pragma once

#include "ArdaRenderGraphDefinitions.h"

namespace arda::render_graph
{
    // One state classification contract for setup, compilation, and execution.
    inline constexpr uint32_t WriteMask =
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::UnorderedAccess) |
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::RenderTarget) |
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::DepthWrite) |
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::CopyDest) |
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::ResolveDest) |
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::AccelStructWrite);

    inline constexpr uint32_t CopyMask =
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::CopySource) |
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::CopyDest);

    inline constexpr uint32_t GraphicsOnlyMask =
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::RenderTarget) |
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::DepthWrite) |
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::DepthRead) |
        static_cast<uint32_t>(rhi::EArdaRHIResourceState::Present);

    [[nodiscard]] inline bool IsWriteState(
        rhi::EArdaRHIResourceState State) noexcept
    {
        return (static_cast<uint32_t>(State) & WriteMask) != 0;
    }

    [[nodiscard]] inline bool IsUAVState(
        rhi::EArdaRHIResourceState State) noexcept
    {
        return (State & rhi::EArdaRHIResourceState::UnorderedAccess) !=
            rhi::EArdaRHIResourceState::Unknown;
    }

    [[nodiscard]] inline rhi::EArdaRHIResourceState NormalizeStateForPipeline(
        rhi::EArdaRHIResourceState State,
        EARDGPipeline Pipeline) noexcept
    {
        if (Pipeline != EARDGPipeline::AsyncCompute ||
            (State & rhi::EArdaRHIResourceState::PixelShaderResource) ==
                rhi::EArdaRHIResourceState::Unknown ||
            (State & rhi::EArdaRHIResourceState::NonPixelShaderResource) ==
                rhi::EArdaRHIResourceState::Unknown)
        {
            return State;
        }

        return static_cast<rhi::EArdaRHIResourceState>(
            static_cast<uint32_t>(State) &
            ~static_cast<uint32_t>(
                rhi::EArdaRHIResourceState::PixelShaderResource));
    }

    [[nodiscard]] inline rhi::EArdaRHIResourceState NormalizeInitialState(
        rhi::EArdaRHIResourceState State) noexcept
    {
        return State == rhi::EArdaRHIResourceState::Unknown
            ? rhi::EArdaRHIResourceState::Common
            : State;
    }
}
