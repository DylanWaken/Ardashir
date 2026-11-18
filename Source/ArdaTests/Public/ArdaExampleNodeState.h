#pragma once

#include "RHI/Device/ArdaRHIDevice.h"
#include <EASTL/shared_ptr.h>

namespace arda
{
	/** Build and publish a device-backed example node's immutable state. */
	template <class State>
	TArdaRHIResult<eastl::shared_ptr<const State>> PrepareExampleNodeState(FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This node requires an initialized device.")};
		}
		auto Prepared = eastl::make_shared<State>();
		Prepared->mDevice = eastl::move(Device);
		if (auto Status = Prepared->Initialize(); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {eastl::move(Prepared), {}};
	}
}
