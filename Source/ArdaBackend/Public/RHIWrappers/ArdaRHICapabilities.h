#pragma once

#include "ArdaRHITypes.h"

namespace arda::rhi
{
    struct FArdaRHICapabilities
    {
        bool mbGraphicsQueue = true;
        bool mbComputeQueue = false;
        bool mbCopyQueue = false;
        bool mbConservativeRasterization = false;
        bool mbMeshShaders = false;
        bool mbRayTracing = false;
        bool mbSamplerFeedback = false;
        bool mbVariableRateShading = false;
        bool mbVirtualResources = false;
        bool mbHeaps = false;
        bool mbStagingTextures = true;
        bool mbQueries = true;
        bool mbBindless = false;
        bool mbShaderLibraries = true;
        bool mbRayTracingAccelStruct = false;
        bool mbRayTracingOpacityMicromap = false;
        bool mbTiledTextures = false;
        bool mbWorkGraphs = false;
        bool mbShaderBundles = false;

        [[nodiscard]] bool IsQueueSupported(EArdaRHIQueueType Queue) const noexcept
        {
            switch (Queue)
            {
            case EArdaRHIQueueType::Graphics: return mbGraphicsQueue;
            case EArdaRHIQueueType::Compute: return mbComputeQueue;
            case EArdaRHIQueueType::Copy: return mbCopyQueue;
            }
            return false;
        }
    };
}
