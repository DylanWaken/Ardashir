#pragma once

#include "RHI/ArdaRHICuda.h"

namespace arda
{
    // Operand launches and prepared RHI kernels share argument/geometry rules.
    // Geometry and parameter patches are checked before native recording.
    template<typename LaunchType>
    FArdaRHIStatus ValidateArdaCudaLaunch(const LaunchType& Launch, size_t BindingCount,
        const FArdaCudaCapabilities& Capabilities)
    {
        if (Launch.mSharedMemoryBytes > Capabilities.mMaxSharedMemoryBytes)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                "CUDA shared-memory requirement is invalid.");
        }
        uint64_t Threads = 1;
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            if (!Launch.mGridSize[Axis] || Launch.mGridSize[Axis] > Capabilities.mMaxGridSize[Axis] ||
                !Launch.mBlockSize[Axis] || Launch.mBlockSize[Axis] > Capabilities.mMaxBlockSize[Axis])
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "CUDA launch dimensions exceed the device limits.");
            }
            // Check before multiplying, including capabilities supplied by custom providers.
            if (Threads > Capabilities.mMaxThreadsPerBlock / Launch.mBlockSize[Axis])
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "CUDA block has too many threads.");
            }
            Threads *= Launch.mBlockSize[Axis];
        }
        for (size_t I = 0; I < Launch.mPatches.size(); ++I)
        {
            const auto& P = Launch.mPatches[I];
            if (P.mBindingIndex >= BindingCount || P.mOffset > Launch.mParameters.size() ||
                sizeof(uint64_t) > Launch.mParameters.size() - P.mOffset ||
                !P.mAlignment || (P.mAlignment & (P.mAlignment - 1)))
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "CUDA parameter resource patch is outside the argument or has invalid alignment.");
            }
            for (size_t J = 0; J < I; ++J)
                if (P.mOffset < Launch.mPatches[J].mOffset + sizeof(uint64_t) &&
                    Launch.mPatches[J].mOffset < P.mOffset + sizeof(uint64_t))
                    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA resource patches overlap.");
        }
        return {};
    }
}
