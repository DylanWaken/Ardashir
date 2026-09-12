#pragma once
#include "ArdaDependencyNode.h"

namespace arda
{
	constexpr uint32_t ArdaTerrainHeightmapWidth = 128 * 5;
	constexpr uint32_t ArdaTerrainHeightmapHeight = 128 * 5;
	constexpr uint32_t ArdaTerrainCellCount = (ArdaTerrainHeightmapWidth - 1) * (ArdaTerrainHeightmapHeight - 1);
	constexpr uint32_t ArdaTerrainVertexCount = ArdaTerrainCellCount * 4;
	constexpr uint32_t ArdaTerrainIndexCount = ArdaTerrainCellCount * 6;

	struct alignas(16) FArdaTerrainSettings
	{
		uint32_t mWidth = ArdaTerrainHeightmapWidth;
		uint32_t mHeight = ArdaTerrainHeightmapHeight;
		float mFrequency = 3.25f;
		float mAmplitude = 0.95f;
		float mTime = 0.0f;
		float mPadding[3] = {};
	};

	struct FArdaTerrainVertex
	{
		float mPosition[3];
		float mHeight;
	};

	struct alignas(16) FArdaTerrainCameraSettings
	{
		float mWorldToView[16];
		float mProjection[16];
	};

	// These CPU inputs are deliberately dynamic. RenderFrame updates them only before
	// synchronous Execute; graph topology, resource handles and pipeline settings stay frozen.
	struct FArdaTerrainFrameInputs
	{
		FArdaTerrainSettings mSettings;
		FArdaTerrainCameraSettings mCamera{};
		eastl::shared_ptr<eastl::vector<uint8_t>> mVertexReadback = eastl::make_shared<eastl::vector<uint8_t>>();
		eastl::shared_ptr<eastl::vector<uint8_t>> mIndexReadback = eastl::make_shared<eastl::vector<uint8_t>>();
	};

}
