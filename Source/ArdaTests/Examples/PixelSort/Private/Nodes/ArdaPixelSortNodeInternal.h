#pragma once
#include "Nodes/ArdaPixelSortNodes.h"

#include "ArdaExamplePaths.h"
#include "ArdaExampleStatus.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"
#include <stdexcept>
#include <string>

namespace arda
{
	inline FArdaRHIStatus ValidatePixelSortExtent(uint32_t Width, uint32_t Height)
	{
		if (!Width || !Height)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "PixelSort requires a nonempty extent.");
		}
		return {};
	}

	inline FArdaRHIStatus PixelSortShaderError(const FArdaGlobalShaderMap& Map)
	{
		const auto Diagnostics = Map.GetDiagnostics();
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
		    Diagnostics.empty() ? "PixelSort node shader initialization failed." : Diagnostics.back().mMessage.c_str());
	}

	inline const char* GetPixelSortShaderSource()
	{
		// Register an executable-relative physical source without mutating the frozen source-directory registry.
		static const std::string Source = (GetArdaExampleDirectory() / "Nodes/Shaders/ArdaPixelSort.hlsl").string();
		return Source.c_str();
	}
}
