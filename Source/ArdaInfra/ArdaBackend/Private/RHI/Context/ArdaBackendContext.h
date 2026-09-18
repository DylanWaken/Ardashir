/** Process-wide backend configuration, device ownership and initialization state. */
#pragma once

#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Device/ArdaBackendDevice.h"
#include "RHI/Config/ArdaDefaultMessageCallback.h"
#include <mutex>

namespace arda
{
	struct FArdaOwnedBackendDevice
	{
		eastl::unique_ptr<IArdaBackendRuntime> mRuntime;
		FArdaBackendDevice mPublic;
	};

	struct FArdaBackendContext
	{
		std::mutex mMutex;
		FArdaBackendConfiguration mConfiguration;
		FArdaDefaultMessageCallback mDefaultMessageCallback;
		eastl::vector<FArdaOwnedBackendDevice> mDevices;
		IArdaExternalDeviceProvider* mExternalDeviceProvider = nullptr;
		eastl::string mError;
		EArdaInitializeResult mInitializeResult = EArdaInitializeResult::Unavailable;
	};


	FArdaBackendContext& GetBackendContext();
	void SetBackendError(const char* Error);
}
