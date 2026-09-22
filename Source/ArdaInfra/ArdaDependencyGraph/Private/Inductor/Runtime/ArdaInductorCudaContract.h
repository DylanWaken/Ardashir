#pragma once

#include "Graph/Resources/ArdaDependencyResource.h"
#include "RHI/CUDA/ArdaRHICuda.h"

namespace arda
{
	/** One node-declared access resolved to its current frame's retained native allocation. */
	struct FArdaInductorCudaResourceAccess
	{
		FArdaRHIResourceRef mResource;
		FArdaDependencyAccess mAccess;
	};

	/** Verifies actual frozen operand bindings against the current node's declarations before dispatch.
	 * Multiple declared ranges may cover a binding; each required read/write permission is checked separately.
	 */
	[[nodiscard]] FArdaRHIStatus ValidateArdaInductorCudaBindings(const eastl::vector<FArdaCudaBinding>& Bindings,
	    const eastl::vector<FArdaInductorCudaResourceAccess>& Declared);
}
