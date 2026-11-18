/** Recorded host copies retained until the provider submission completes. */
#pragma once

#include "RHI/Scheduling/ArdaRHICommandList.h"
#include "RHI/Providers/ArdaRHIProviderDevice.h"

namespace arda::detail
{
	struct FArdaPendingBufferCopyCompletion
	{
		bool mbBlocking = false;
		FArdaProviderObjectRef mReadbackBuffer;
		size_t mByteSize = 0;
		eastl::vector<uint8_t>* mOutput = nullptr;
		FArdaRHIHostToDeviceCopyCallback mUploadCallback;
		FArdaRHIDeviceToHostCopyCallback mReadbackCallback;
	};
}
