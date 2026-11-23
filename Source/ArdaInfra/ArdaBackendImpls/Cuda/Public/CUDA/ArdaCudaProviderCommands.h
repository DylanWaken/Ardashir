/** Device-provider arguments for the typed CUDA recording compatibility path. */
#pragma once

#include "CUDA/ArdaRHICuda.h"
#include "RHI/Providers/ArdaProviderObject.h"

namespace arda
{
	struct FArdaProviderCudaBinding
	{
		FArdaProviderObjectRef mObject;
		EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
		EArdaComputeBindingType mType = EArdaComputeBindingType::Buffer;
		FArdaRHIBufferRange mBufferRange;
		uint32_t mMipLevel = 0;
	};
}
