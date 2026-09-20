#pragma once

#include "RHI/Config/ArdaRHIStatus.h"

namespace arda
{
	inline FArdaRHIStatus MakeArdaInductorRuntimeError(const char* Message)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, Message);
	}
}
