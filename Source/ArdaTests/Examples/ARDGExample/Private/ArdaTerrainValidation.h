#pragma once

#include "Nodes/ArdaTerrainNodeParameters.h"

namespace arda
{
	eastl::string ValidateTerrainReadback(const eastl::vector<uint8_t>& VertexBytes,
	    const eastl::vector<uint8_t>& IndexBytes,
	    const FArdaTerrainSettings& Settings);

	/** Check the initial default-camera terrain view for visible red and neutral contours.
	 * ColorBytes must contain tightly packed RGBA8/BGRA8 UNorm or sRGB pixels. This
	 * smoke check detects blank or uniformly red output; it is not an image-quality
	 * metric and must not be applied to arbitrary camera views or terrain settings.
	 */
	eastl::string ValidateTerrainColorReadback(const eastl::vector<uint8_t>& ColorBytes,
	    uint32_t Width,
	    uint32_t Height,
	    EArdaRHIFormat Format);

	/** Summarize a diagnostic rerun without dumping resource contents. Heightmaps
	 * contain tightly packed R32Float texels at the example's fixed dimensions.
	 * Reports malformed readbacks safely and compares the first mesh cell against
	 * the eroded texture to distinguish bad generation from later sampling errors.
	 */
	eastl::string DescribeTerrainStageReadbacks(const eastl::vector<uint8_t>& SettingsUploadBytes,
	    const eastl::vector<uint8_t>& SettingsCopyBytes,
	    const eastl::vector<uint8_t>& RawHeightmapBytes,
	    const eastl::vector<uint8_t>& ErodedHeightmapBytes,
	    const eastl::vector<uint8_t>& VertexBytes,
	    const FArdaTerrainSettings& ExpectedSettings);
}
