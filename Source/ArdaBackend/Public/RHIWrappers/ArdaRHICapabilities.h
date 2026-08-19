/** @file ArdaRHICapabilities.h
 * Declares feature and queue capabilities reported by an RHI device.
 */

#pragma once

#include "ArdaRHITypes.h"

namespace arda::rhi
{
    /** Describes capabilities. */
    struct FArdaRHICapabilities
    {
        /** Stores the graphics queue. */
        bool mbGraphicsQueue = true;
        /** Stores the compute queue. */
        bool mbComputeQueue = false;
        /** Stores the copy queue. */
        bool mbCopyQueue = false;
        /** Stores the conservative rasterization. */
        bool mbConservativeRasterization = false;
        /** Stores the mesh shaders. */
        bool mbMeshShaders = false;
        /** Stores the ray tracing. */
        bool mbRayTracing = false;
        /** Stores the sampler feedback. */
        bool mbSamplerFeedback = false;
        /** Stores the variable rate shading. */
        bool mbVariableRateShading = false;
        /** Stores the virtual resources. */
        bool mbVirtualResources = false;
        /** Stores the heaps. */
        bool mbHeaps = false;
        /** Stores the staging textures. */
        bool mbStagingTextures = true;
        /** Stores the queries. */
        bool mbQueries = true;
        /** Stores the bindless. */
        bool mbBindless = false;
        /** Stores the shader libraries. */
        bool mbShaderLibraries = true;
        /** Stores the ray tracing accel struct. */
        bool mbRayTracingAccelStruct = false;
        /** Stores the ray tracing opacity micromap. */
        bool mbRayTracingOpacityMicromap = false;
        /** Stores the tiled textures. */
        bool mbTiledTextures = false;
        /** Stores the work graphs. */
        bool mbWorkGraphs = false;
        /** Stores the shader bundles. */
        bool mbShaderBundles = false;
        /** True when the backend can persist native pipeline-cache data. */
        bool mbPipelineCachePersistence = false;

        /**
         * Tests whether the queue supported.
         * @param Queue The queue.
         * @return True when the condition is satisfied; otherwise false.
         */
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
