#pragma once

#include "RHI/ArdaRHIDevice.h"

#include <stdexcept>

namespace arda
{
	/** Propagates failures through the exception boundary used by interactive examples. */
	inline void CheckArdaExampleStatus(const FArdaRHIStatus& Status)
	{
		if (!Status)
		{
			throw std::runtime_error(Status.mMessage.c_str());
		}
	}

	/** Transfers a successful result without copying retained graphics objects. */
	template <class T>
	T TakeArdaExampleValue(TArdaRHIResult<T> Result)
	{
		CheckArdaExampleStatus(Result.mStatus);
		return eastl::move(Result.mValue);
	}
}
