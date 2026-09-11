#pragma once

#include "ArdaRenderGraphDefinitions.h"
#include "ArdaRenderGraphResources.h"
#include "ArdaRenderGraphParameters.h"
#include "ArdaRenderGraphPass.h"
#include "ArdaRenderGraphBlackboard.h"
#include "ArdaRenderGraphBuilder.h"
#include "ArdaRenderGraphCuda.h"
#include "ArdaBackend.h"

namespace arda
{
	[[nodiscard]] inline FARDGRenderGraphContext MakeRenderGraphContext(arda::FArdaRHIDeviceRef Device,
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
	[[nodiscard]] const char* GetRenderGraphModuleName() noexcept;
}
