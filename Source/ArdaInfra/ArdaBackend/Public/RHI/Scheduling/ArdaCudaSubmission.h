/** CUDA operand submission identity and the selected kernel variant. */
#pragma once

#include "RHI/CUDA/ArdaCudaKernelVariants.h"
#include "RHI/Device/ArdaRHIDevice.h"
#include "RHI/Scheduling/ArdaRHIQueueTypes.h"

namespace arda
{
	/** Queue completion identity. Zero denotes explicit NoWork. */
	struct FArdaCudaSubmission
	{
		FArdaRHIDeviceRef mDevice;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
		uint64_t mInstance = 0;
		FArdaCudaKernelSelection mSelection;
	};
}
