#pragma once
#include "ArdaDependencyNode.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

#ifndef ARDA_RDG_RECIPE_SHADER_DIR
#error "Set ARDA_RDG_RECIPE_SHADER_DIR to this recipe's absolute Shaders directory."
#endif

namespace arda
{
	// Private authoring utilities, not renderer-side initialization or public graph APIs.
	inline FArdaRHIStatus ShaderError(const FArdaGlobalShaderMap& Map)
	{
		const auto Diagnostics = Map.GetDiagnostics();
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
		    Diagnostics.empty() ? "Recipe shader setup failed." : Diagnostics.back().mMessage.c_str());
	}
}
