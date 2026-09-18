/** Internal portable status construction helpers. */
#pragma once

#include "RHI/Config/ArdaRHIStatus.h"

namespace arda::detail
{
	inline FArdaRHIStatus Invalid(const char* Message)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
	}

	inline FArdaRHIStatus Unsupported(const char* Message)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Message);
	}

	inline FArdaRHIStatus WrongDevice()
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
		    "Resource belongs to another RHI device or implementation.");
	}

	template <typename T>
	TArdaRHIResult<T> Failure(FArdaRHIStatus Status)
	{
		return {{}, eastl::move(Status)};
	}

	template <typename T>
	TArdaRHIResult<T> UnsupportedResult(const char* Message)
	{
		return Failure<T>(Unsupported(Message));
	}
}
