/** Internal resource casts and access to retained provider objects. */
#pragma once

#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Providers/ArdaProviderObject.h"

namespace arda::detail
{
	template <typename T>
	T* Cast(IArdaRHIResource* Resource) noexcept
	{
		return dynamic_cast<T*>(Resource);
	}

	template <typename T>
	const T* Cast(const IArdaRHIResource* Resource) noexcept
	{
		return dynamic_cast<const T*>(Resource);
	}


	FArdaProviderObjectRef GetNativeObject(IArdaRHIResource* Resource);
}
