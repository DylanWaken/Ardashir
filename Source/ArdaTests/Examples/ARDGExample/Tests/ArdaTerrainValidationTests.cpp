#include "ArdaTerrainValidation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	struct FArdaTerrainReadback
	{
		eastl::vector<uint8_t> mVertices;
		eastl::vector<uint8_t> mIndices;
	};

	template <typename HeightFunction>
	FArdaTerrainReadback MakeTerrain(HeightFunction HeightAt)
	{
		FArdaTerrainReadback Result;
		Result.mVertices.resize(static_cast<size_t>(ArdaTerrainVertexCount) * sizeof(FArdaTerrainVertex));
		Result.mIndices.resize(static_cast<size_t>(ArdaTerrainIndexCount) * sizeof(uint32_t));
		constexpr uint32_t CornerX[4] = {0, 1, 0, 1}, CornerY[4] = {0, 0, 1, 1};
		constexpr uint32_t LocalIndices[6] = {0, 2, 1, 1, 2, 3};
		for (uint32_t Cell = 0; Cell < ArdaTerrainCellCount; ++Cell)
		{
			for (uint32_t Corner = 0; Corner < 4; ++Corner)
			{
				const float U = static_cast<float>(Cell % (ArdaTerrainHeightmapWidth - 1) + CornerX[Corner]) /
				    static_cast<float>(ArdaTerrainHeightmapWidth - 1);
				const float V = static_cast<float>(Cell / (ArdaTerrainHeightmapWidth - 1) + CornerY[Corner]) /
				    static_cast<float>(ArdaTerrainHeightmapHeight - 1);
				const float Height = HeightAt(U, V);
				const FArdaTerrainVertex Vertex{{(V - 0.5f) * 1.45f, (U - 0.5f) * 1.45f, Height * 0.72f - 0.32f},
				    Height};
				std::memcpy(Result.mVertices.data() + (Cell * 4 + Corner) * sizeof(Vertex), &Vertex, sizeof(Vertex));
			}
			for (uint32_t Index = 0; Index < 6; ++Index)
			{
				const uint32_t Value = Cell * 4 + LocalIndices[Index];
				std::memcpy(Result.mIndices.data() + (Cell * 6 + Index) * sizeof(Value), &Value, sizeof(Value));
			}
		}
		return Result;
	}

	TEST(ArdaTerrainValidation, RejectsTriangulatedAllZeroHeightmap)
	{
		const auto Readback = MakeTerrain(
		    [](float, float)
		    {
			    return 0.0f;
		    });
		EXPECT_FALSE(ValidateTerrainReadback(Readback.mVertices, Readback.mIndices, {}).empty());
	}

	TEST(ArdaTerrainValidation, RejectsFlatBoundaryHeightWhenNoiseWasRequested)
	{
		const auto Readback = MakeTerrain(
		    [](float, float)
		    {
			    return 0.42f;
		    });
		EXPECT_FALSE(ValidateTerrainReadback(Readback.mVertices, Readback.mIndices, {}).empty());
	}

	TEST(ArdaTerrainValidation, RejectsMissingGenerationEvenWhenErosionProducesValidBoundaryAndSlopes)
	{
		const auto Readback = MakeTerrain(
		    [](float U, float V)
		    {
			    const float EdgeDistance = std::min(std::min(U, 1.0f - U), std::min(V, 1.0f - V));
			    const float T = std::clamp(EdgeDistance / 0.12f, 0.0f, 1.0f);
			    return 0.42f * (1.0f - T * T * (3.0f - 2.0f * T));
		    });
		EXPECT_FALSE(ValidateTerrainReadback(Readback.mVertices, Readback.mIndices, {}).empty());
	}

	TEST(ArdaTerrainValidation, AcceptsFlatTerrainWhenZeroAmplitudeWasRequested)
	{
		const auto Readback = MakeTerrain(
		    [](float, float)
		    {
			    return 0.42f;
		    });
		FArdaTerrainSettings Settings;
		Settings.mAmplitude = 0.0f;
		const auto Error = ValidateTerrainReadback(Readback.mVertices, Readback.mIndices, Settings);
		EXPECT_TRUE(Error.empty()) << Error.c_str();
	}

	TEST(ArdaTerrainValidation, AcceptsKnownNoiseHeightsForCurrentFrameSettings)
	{
		// At frequency zero, each octave samples one lattice point. These golden
		// interior heights use the fixed hashes at z={0,19,38} and z={11,30,49}.
		const float Times[] = {0.0f, 50.0f};
		const float InteriorHeights[] = {0.295219073f, 0.148004904f};
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			const auto Readback = MakeTerrain(
			    [InteriorHeight = InteriorHeights[Frame]](float U, float V)
			    {
				    const float EdgeDistance = std::min(std::min(U, 1.0f - U), std::min(V, 1.0f - V));
				    const float T = std::clamp(EdgeDistance / 0.12f, 0.0f, 1.0f);
				    return 0.42f + (InteriorHeight - 0.42f) * T * T * (3.0f - 2.0f * T);
			    });
			FArdaTerrainSettings Settings;
			Settings.mFrequency = 0.0f;
			Settings.mTime = Times[Frame];
			const auto Error = ValidateTerrainReadback(Readback.mVertices, Readback.mIndices, Settings);
			EXPECT_TRUE(Error.empty()) << Error.c_str();
			Settings.mTime = Times[1 - Frame];
			EXPECT_FALSE(ValidateTerrainReadback(Readback.mVertices, Readback.mIndices, Settings).empty());
		}
	}

	TEST(ArdaTerrainValidation, RejectsMissingReadback)
	{
		EXPECT_FALSE(ValidateTerrainReadback({}, {}, {}).empty());
	}

	TEST(ArdaTerrainValidation, RejectsCorruptTopologyEvenWhenHeightsMatch)
	{
		auto Readback = MakeTerrain(
		    [](float, float)
		    {
			    return 0.42f;
		    });
		const uint32_t WrongIndex = 1;
		std::memcpy(Readback.mIndices.data(), &WrongIndex, sizeof(WrongIndex));
		FArdaTerrainSettings Settings;
		Settings.mAmplitude = 0.0f;
		const auto Error = ValidateTerrainReadback(Readback.mVertices, Readback.mIndices, Settings);
		EXPECT_NE(Error.find("index readback diverged"), eastl::string::npos) << Error.c_str();
	}
}
