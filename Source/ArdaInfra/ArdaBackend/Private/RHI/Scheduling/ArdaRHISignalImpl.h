/** Event-query, GPU-timer and GPU-fence facade resources. */
#pragma once

#include "RHI/Resources/ArdaRHIResourceImpl.h"

namespace arda::detail
{
	template <typename Interface, EArdaRHIResourceType Type>
	class TArdaNativeSignal final : public FArdaResource, public Interface
	{
	public:
		TArdaNativeSignal(const char* Name,
		    FArdaProviderObjectRef Native,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(Type, Name, Owner, eastl::move(LifetimeTracker)),
		      mNative(eastl::move(Native))
		{
		}

		FArdaProviderObjectRef mNative;
	};

	using FArdaEventQuery = TArdaNativeSignal<IArdaRHIEventQuery, EArdaRHIResourceType::EventQuery>;
	using FArdaTimerQuery = TArdaNativeSignal<IArdaRHITimerQuery, EArdaRHIResourceType::TimerQuery>;
	using FArdaGpuFence = TArdaNativeSignal<IArdaRHIGpuFence, EArdaRHIResourceType::GpuFence>;
}
