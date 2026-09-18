#pragma once
#include "Nodes/ArdaTerrainNodeParameters.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"

namespace arda
{
	inline FArdaRHIStatus TerrainShaderError(const FArdaGlobalShaderMap& Map)
	{
		const auto Diagnostics = Map.GetDiagnostics();
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
		    Diagnostics.empty() ? "Terrain node shader initialization failed." : Diagnostics.back().mMessage.c_str());
	}

	constexpr uint32_t DivideRoundUp(uint32_t Value, uint32_t Divisor)
	{
		return (Value + Divisor - 1) / Divisor;
	}

}
