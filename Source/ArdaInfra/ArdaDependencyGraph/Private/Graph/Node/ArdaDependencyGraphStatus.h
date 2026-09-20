/** Shared edit-state and argument diagnostics for graph authoring operations. */
#pragma once

#include "RHI/Config/ArdaRHIStatus.h"

namespace arda
{
	inline FArdaRHIStatus GraphInvalid(const char* Text)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Text);
	}

	inline FArdaRHIStatus GraphNotEditing()
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
		    "Modify the dependency graph only between BeginGraphEdit and EndGraphEdit.");
	}

	inline FArdaRHIStatus GraphCompiling()
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
		    "Graph authoring and recursive compilation are unavailable while compiler rules are running.");
	}
}
