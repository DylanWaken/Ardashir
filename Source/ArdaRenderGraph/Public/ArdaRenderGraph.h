#pragma once

#include "ArdaRenderGraphDefinitions.h"
#include "ArdaRenderGraphResources.h"
#include "ArdaRenderGraphParameters.h"
#include "ArdaRenderGraphPass.h"
#include "ArdaRenderGraphBlackboard.h"
#include "ArdaRenderGraphBuilder.h"
#include "ArdaBackend.h"

namespace arda::render_graph
{
    [[nodiscard]] inline FARDGRenderGraphContext MakeRenderGraphContext(
        rhi::FArdaRHIDeviceRef Device,
        FARDGDebugOptions DebugOptions = {})
    {
        FARDGRenderGraphContext Result;
        Result.mDevice = eastl::move(Device);
        if (Result.mDevice)
        {
            const auto& Queues = Result.mDevice->GetCapabilities().mQueues;
            Result.mQueuePolicy.mbGraphics = Queues.mbGraphics;
            Result.mQueuePolicy.mbCompute = Queues.mbCompute;
            Result.mQueuePolicy.mbCopy = Queues.mbCopy;
        }
        Result.mDebugOptions = DebugOptions;
        return Result;
    }

    /** Returns the stable name of the render-graph module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
