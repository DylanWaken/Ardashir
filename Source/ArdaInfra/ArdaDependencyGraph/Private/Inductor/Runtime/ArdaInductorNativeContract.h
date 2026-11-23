#pragma once

#include "NativeCompute/ArdaNativeOperand.h"
#include "Graph/Resources/ArdaDependencyResource.h"

namespace arda
{
	/** One node-declared access resolved to its current frame's retained allocation. */
	struct FArdaInductorNativeResourceAccess
	{
		FArdaRHIResourceRef mResource;
		FArdaDependencyAccess mAccess;
	};

	enum class EArdaInductorNativeBindingDomain : uint8_t
	{
		Any,
		Cuda,
		ComputeShader,
		GraphicsShader
	};

	[[nodiscard]] EArdaInductorNativeBindingDomain GetArdaInductorNativeBindingDomain(EArdaNativeComputeRoute Route);
	[[nodiscard]] bool AllowsArdaInductorNativeResourceState(EArdaNativeComputeRoute Route,
	    EArdaComputeAccess Access,
	    EArdaRHIResourceState Declared,
	    EArdaRHIResourceState Required);

	[[nodiscard]] FArdaRHIStatus ValidateArdaInductorNativeBindings(const FArdaNativeOperand& Operand,
	    const FArdaNativeInvocation& Invocation,
	    const eastl::vector<FArdaInductorNativeResourceAccess>& Declared,
	    EArdaInductorNativeBindingDomain Domain,
	    const eastl::vector<EArdaRHIResourceState>& RequiredStates = {});
}
