#pragma once

#include "CUDA/ArdaCudaModule.h"

namespace arda
{
	[[nodiscard]] bool HasArdaNativeCudaCompiler() noexcept;
	[[nodiscard]] FArdaRHIStatus CheckArdaNativeCudaCompilerTarget(uint32_t ComputeCapability);
	[[nodiscard]] TArdaRHIResult<FArdaCudaModuleDesc> CompileArdaNativeCudaSource(const eastl::string& Source,
	    uint32_t ComputeCapability);
}
