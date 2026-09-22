#pragma once

#include "Graph/Node/ArdaDependencyNodeDesc.h"

namespace arda
{
	/** Checks prepared shader contracts independently of graph arguments. */
	FArdaRHIStatus ValidateArdaPipelineShaderContract(const FArdaInductorPipelineConfiguration& Configuration);
	/** Checks complete schema coverage and required access before graph compilation succeeds. */
	FArdaRHIStatus ValidateArdaNodeShaderContract(const FArdaDependencyNodeDesc& Description,
	    const FArdaInductorPipelineConfiguration& Configuration,
	    const eastl::string& Slot);
}
