#pragma once

#include "RHI/ArdaRHICuda.h"

namespace arda
{
    // Operand launches and prepared RHI kernels share argument/geometry rules.
    // Code validation is separate so malformed calls never trigger compilation.
    template<typename LaunchType>
    FArdaRHIStatus ValidateArdaCudaLaunch(const LaunchType& Launch, size_t BindingCount,
        const FArdaCudaCapabilities& Capabilities)
    {
        if (Launch.mEntryPoint.empty() || Launch.mEntryPoint.find('\0') != eastl::string::npos ||
            Launch.mSharedMemoryBytes > Capabilities.mMaxSharedMemoryBytes)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                "CUDA entry point or shared-memory requirement is invalid.");
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
        for (const auto& Argument : Launch.mArguments)
        {
            if ((Argument.mBindingIndex == UINT32_MAX && Argument.mValue.empty()) ||
                (Argument.mBindingIndex != UINT32_MAX &&
                    (Argument.mBindingIndex >= BindingCount || !Argument.mValue.empty())))
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "CUDA argument must reference a binding or contain owned value bytes.");
            }
        }
        return {};
    }
}
