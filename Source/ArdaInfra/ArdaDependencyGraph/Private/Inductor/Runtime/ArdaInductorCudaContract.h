#pragma once

#include "Graph/Resources/ArdaDependencyResource.h"
#include "RHI/CUDA/ArdaRHICuda.h"

namespace arda
{
	struct FArdaNativeOperandDesc;
	struct FArdaNativeInvocation;

	/** One node-declared access resolved to its current frame's retained native allocation. */
	struct FArdaInductorNativeResourceAccess
	{
		FArdaRHIResourceRef mResource;
		FArdaDependencyAccess mAccess;
	};
	using FArdaInductorCudaResourceAccess = FArdaInductorNativeResourceAccess;
	enum class EArdaInductorNativeBindingDomain : uint8_t
	{
		Any,
		Cuda,
		ComputeShader,
		GraphicsShader
	};

	/** Verifies actual frozen operand bindings against the current node's declarations before dispatch.
	 * Multiple declared ranges may cover a binding; each required read/write permission is checked separately.
	 */
	[[nodiscard]] FArdaRHIStatus ValidateArdaInductorCudaBindings(const eastl::vector<FArdaCudaBinding>& Bindings,
	    const eastl::vector<FArdaInductorCudaResourceAccess>& Declared);

	[[nodiscard]] FArdaRHIStatus ValidateArdaInductorNativeBindings(const FArdaNativeOperandDesc& Operand,
	    const FArdaNativeInvocation& Invocation,
	    const eastl::vector<FArdaInductorNativeResourceAccess>& Declared,
	    EArdaInductorNativeBindingDomain Domain,
	    const eastl::vector<EArdaRHIResourceState>& RequiredStates = {});
}
