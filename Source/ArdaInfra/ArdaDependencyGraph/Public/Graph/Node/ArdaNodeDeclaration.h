#pragma once

#include "ArdaGraph.h"
#include <cstdint>

namespace arda
{
	/** Stable logical source for target-device reconstruction of a node's declaration objects.
	 * Copies retain this identity when preprocessing reorders or removes declarations.
	 */
	struct FArdaNodeDeclarationOrigin
	{
		FArdaGraphNodeHandle mNode;
		uint32_t mIndex = UINT32_MAX;
		/** Shader-table index for a local binding; zero marks an assembled pipeline request. */
		uint32_t mParentIndex = UINT32_MAX;
	};
}
