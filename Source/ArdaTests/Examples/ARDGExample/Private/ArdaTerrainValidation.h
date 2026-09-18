#pragma once

#include "Nodes/ArdaTerrainNodeParameters.h"

namespace arda
{
	eastl::string ValidateTerrainReadback(const eastl::vector<uint8_t>& VertexBytes,
	    const eastl::vector<uint8_t>& IndexBytes,
	    const FArdaTerrainSettings& Settings);
}
