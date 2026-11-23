#pragma once

#include "CUDA/ArdaRHICuda.h"
#include "Inductor/Runtime/ArdaInductorNativeContract.h"

namespace arda
{
	using FArdaInductorCudaResourceAccess = FArdaInductorNativeResourceAccess;

	/** Verifies actual frozen operand bindings against the current node's declarations before dispatch.
	 * Multiple declared ranges may cover a binding; each required read/write permission is checked separately.
	 */
	[[nodiscard]] FArdaRHIStatus ValidateArdaInductorCudaBindings(const eastl::vector<FArdaCudaBinding>& Bindings,
	    const eastl::vector<FArdaInductorCudaResourceAccess>& Declared);

}
