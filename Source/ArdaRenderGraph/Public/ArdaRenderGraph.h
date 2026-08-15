#pragma once

#include "ArdaRenderGraphDefinitions.h"
#include "ArdaRenderGraphResources.h"
#include "ArdaRenderGraphParameters.h"
#include "ArdaRenderGraphPass.h"
#include "ArdaRenderGraphBlackboard.h"
#include "ArdaRenderGraphBuilder.h"
#include "ArdaDevice.h"

namespace arda::render_graph
{
    [[nodiscard]] inline FARDGRenderGraphContext MakeRenderGraphContext(
        const backend::FArdaDeviceContext& DeviceContext,
        FARDGDebugOptions DebugOptions = {})
    {
        FARDGRenderGraphContext Result;
        Result.mDevice = DeviceContext.mDevice;
        Result.mQueueCapabilities.mbGraphics =
            DeviceContext.mQueueCapabilities.mbGraphics;
        Result.mQueueCapabilities.mbCompute =
            DeviceContext.mQueueCapabilities.mbCompute;
        Result.mQueueCapabilities.mbCopy =
            DeviceContext.mQueueCapabilities.mbCopy;
        Result.mDebugOptions = DebugOptions;
        return Result;
    }

    /** Returns the stable name of the render-graph module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
