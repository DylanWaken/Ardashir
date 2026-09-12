#pragma once
#include "ArdaInductorCommandProgram.h"

namespace arda
{
	/** Records fresh native commands for a previously retired persistent frame slot. */
	class FArdaInductorCommandExecutor final
	{
	public:
		static const FArdaGraphExecutionResult& Submit(FArdaInductorCommandProgram& Program,
		    const FArdaGraphExecuteOptions& Options);
	};
}
