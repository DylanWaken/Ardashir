#include "RHI/Interop/ArdaExternalInterop.h"
#include "RHI/Context/ArdaBackendContext.h"
namespace arda
{
	bool RegisterExternalDeviceProvider(IArdaExternalDeviceProvider& Provider)
	{
		auto& state = GetBackendContext();
		std::lock_guard<std::mutex> lock(state.mMutex);
		if (!state.mDevices.empty())
		{
			state.mError = "External device provider registration cannot change while initialized.";
			return false;
		}
		if (state.mExternalDeviceProvider && state.mExternalDeviceProvider != &Provider)
		{
			state.mError = "A different external device provider is already registered.";
			return false;
		}
		state.mExternalDeviceProvider = &Provider;
		state.mError.clear();
		return true;
	}

	bool UnregisterExternalDeviceProvider(IArdaExternalDeviceProvider& Provider)
	{
		auto& state = GetBackendContext();
		std::lock_guard<std::mutex> lock(state.mMutex);
		if (!state.mDevices.empty())
		{
			state.mError = "External device provider registration cannot change while initialized.";
			return false;
		}
		if (state.mExternalDeviceProvider && state.mExternalDeviceProvider != &Provider)
		{
			state.mError = "The specified external device provider is not registered.";
			return false;
		}
		state.mExternalDeviceProvider = nullptr;
		state.mError.clear();
		return true;
	}

	const IArdaExternalDeviceProvider* GetExternalDeviceProvider() noexcept
	{
		auto& state = GetBackendContext();
		std::lock_guard<std::mutex> lock(state.mMutex);
		return state.mExternalDeviceProvider;
	}

}
