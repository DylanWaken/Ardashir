/** Command validation policy for one graph execution. */
#pragma once

namespace arda
{
	struct FArdaGraphExecuteOptions
	{
		/** Captures and validates RHI/native state at every graph checkpoint. */
		bool mbValidateResourceStates = true;
	};
}
