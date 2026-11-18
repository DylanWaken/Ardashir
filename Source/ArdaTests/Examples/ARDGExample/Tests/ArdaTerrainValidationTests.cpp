#include "ArdaTerrainValidation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

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

	void WriteTerrainColor(eastl::vector<uint8_t>& Bytes,
	    size_t Pixel,
	    EArdaRHIFormat Format,
	    uint8_t Red,
	    uint8_t Green,
	    uint8_t Blue)
	{
		const bool bBlueFirst = Format == EArdaRHIFormat::BGRA8UNorm || Format == EArdaRHIFormat::SBGRA8UNorm;
		Bytes[Pixel * 4 + (bBlueFirst ? 2 : 0)] = Red;
		Bytes[Pixel * 4 + 1] = Green;
		Bytes[Pixel * 4 + (bBlueFirst ? 0 : 2)] = Blue;
		Bytes[Pixel * 4 + 3] = 255;
	}

	TEST(ArdaTerrainValidation, AcceptsVisibleRedAndNeutralContoursInBothChannelOrdersAndEncodings)
	{
		const EArdaRHIFormat Formats[] = {EArdaRHIFormat::RGBA8UNorm,
		    EArdaRHIFormat::BGRA8UNorm,
		    EArdaRHIFormat::SRGBA8UNorm,
		    EArdaRHIFormat::SBGRA8UNorm};
		for (const auto Format : Formats)
		{
			eastl::vector<uint8_t> Bytes(200 * 100 * 4, 0);
			WriteTerrainColor(Bytes, 123, Format, 200, 12, 7);
			WriteTerrainColor(Bytes, 456, Format, 64, 31, 31);
			WriteTerrainColor(Bytes, 789, Format, 192, 190, 194);
			WriteTerrainColor(Bytes, 19999, Format, 72, 64, 64);
			const auto Error = ValidateTerrainColorReadback(Bytes, 200, 100, Format);
			EXPECT_TRUE(Error.empty()) << Error.c_str();
		}
	}

	TEST(ArdaTerrainValidation, RejectsEntirelyRedTerrainColor)
	{
		eastl::vector<uint8_t> Bytes(16 * 16 * 4, 0);
		for (size_t Pixel = 0; Pixel < 16 * 16; ++Pixel)
		{
			WriteTerrainColor(Bytes, Pixel, EArdaRHIFormat::RGBA8UNorm, 230, 7, 4);
		}
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 16, 16, EArdaRHIFormat::RGBA8UNorm).empty());
	}

	TEST(ArdaTerrainValidation, RejectsBlankTerrainColor)
	{
		const eastl::vector<uint8_t> Bytes(16 * 16 * 4, 0);
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 16, 16, EArdaRHIFormat::RGBA8UNorm).empty());
	}

	TEST(ArdaTerrainValidation, RejectsTerrainColorWithoutRedContours)
	{
		eastl::vector<uint8_t> Bytes(16 * 16 * 4, 255);
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 16, 16, EArdaRHIFormat::RGBA8UNorm).empty());
		// Blue contours must not be mistaken for red in either channel order.
		for (const auto Format : {EArdaRHIFormat::RGBA8UNorm, EArdaRHIFormat::BGRA8UNorm})
		{
			WriteTerrainColor(Bytes, 0, Format, 7, 12, 220);
			EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 16, 16, Format).empty());
		}
	}

	TEST(ArdaTerrainValidation, RejectsIsolatedContourPixelsInLargeImage)
	{
		eastl::vector<uint8_t> Bytes(200 * 100 * 4, 0);
		WriteTerrainColor(Bytes, 0, EArdaRHIFormat::RGBA8UNorm, 200, 12, 7);
		WriteTerrainColor(Bytes, 1, EArdaRHIFormat::RGBA8UNorm, 200, 200, 200);
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 200, 100, EArdaRHIFormat::RGBA8UNorm).empty());
		WriteTerrainColor(Bytes, 2, EArdaRHIFormat::RGBA8UNorm, 200, 12, 7);
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 200, 100, EArdaRHIFormat::RGBA8UNorm).empty());
	}

	TEST(ArdaTerrainValidation, RejectsDarkOrWeaklyRedTerrainPixels)
	{
		eastl::vector<uint8_t> Bytes(3 * 4, 0);
		WriteTerrainColor(Bytes, 0, EArdaRHIFormat::RGBA8UNorm, 63, 0, 0);
		WriteTerrainColor(Bytes, 1, EArdaRHIFormat::RGBA8UNorm, 64, 32, 0);
		WriteTerrainColor(Bytes, 2, EArdaRHIFormat::RGBA8UNorm, 200, 200, 200);
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 3, 1, EArdaRHIFormat::RGBA8UNorm).empty());
	}

	TEST(ArdaTerrainValidation, RejectsMalformedTerrainColorReadbacks)
	{
		const eastl::vector<uint8_t> Bytes(2 * 2 * 4, 0);
		EXPECT_FALSE(ValidateTerrainColorReadback({}, 0, 0, EArdaRHIFormat::RGBA8UNorm).empty());
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 0, 2, EArdaRHIFormat::RGBA8UNorm).empty());
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 2, 0, EArdaRHIFormat::RGBA8UNorm).empty());
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 3, 2, EArdaRHIFormat::RGBA8UNorm).empty());
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 1, 2, EArdaRHIFormat::RGBA8UNorm).empty());
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, UINT32_MAX, UINT32_MAX, EArdaRHIFormat::RGBA8UNorm).empty());
		auto Truncated = Bytes;
		Truncated.pop_back();
		EXPECT_FALSE(ValidateTerrainColorReadback(Truncated, 2, 2, EArdaRHIFormat::RGBA8UNorm).empty());
		EXPECT_FALSE(ValidateTerrainColorReadback(Bytes, 2, 2, EArdaRHIFormat::R32Float).empty());
	}

	eastl::vector<uint8_t> TerrainSettingsBytes(const FArdaTerrainSettings& Settings)
	{
		eastl::vector<uint8_t> Bytes(sizeof(Settings));
		std::memcpy(Bytes.data(), &Settings, sizeof(Settings));
		return Bytes;
	}

	eastl::vector<uint8_t> TerrainHeightmapBytes(float Height)
	{
		const size_t Count = static_cast<size_t>(ArdaTerrainHeightmapWidth) * ArdaTerrainHeightmapHeight;
		eastl::vector<uint8_t> Bytes(Count * sizeof(Height));
		for (size_t Pixel = 0; Pixel < Count; ++Pixel)
		{
			std::memcpy(Bytes.data() + Pixel * sizeof(Height), &Height, sizeof(Height));
		}
		return Bytes;
	}

	eastl::vector<uint8_t> TerrainFirstCellBytes(float Height)
	{
		eastl::vector<uint8_t> Bytes(4 * sizeof(FArdaTerrainVertex));
		const FArdaTerrainVertex Vertex{{}, Height};
		for (size_t Corner = 0; Corner < 4; ++Corner)
		{
			std::memcpy(Bytes.data() + Corner * sizeof(Vertex), &Vertex, sizeof(Vertex));
		}
		return Bytes;
	}

	TEST(ArdaTerrainValidation, StageDiagnosticsDistinguishMissingGenerationFromVertexSampling)
	{
		FArdaTerrainSettings Settings;
		Settings.mTime = 2.0f;
		const auto SettingsBytes = TerrainSettingsBytes(Settings);
		const auto Summary = DescribeTerrainStageReadbacks(SettingsBytes,
		    SettingsBytes,
		    TerrainHeightmapBytes(0.0f),
		    TerrainHeightmapBytes(0.42f),
		    TerrainFirstCellBytes(0.42f),
		    Settings);
		EXPECT_NE(Summary.find("Settings upload: bytes=32/32, matches expected=yes"), eastl::string::npos);
		EXPECT_NE(Summary.find("width=640, height=640, frequency=3.25, amplitude="), eastl::string::npos);
		EXPECT_NE(Summary.find("time=2"), eastl::string::npos);
		EXPECT_NE(Summary.find("Settings upload/copy bytes match=yes"), eastl::string::npos);
		EXPECT_NE(Summary.find("Raw heightmap: dimensions=640x640, bytes=1638400/1638400, complete=yes; "
		                       "finite=409600/409600, finite nonzero=0, finite min=0, max=0"),
		    eastl::string::npos);
		EXPECT_NE(Summary.find("eroded texture/vertex matches=yes, mismatched corners=0, missing corners=0"),
		    eastl::string::npos);
	}

	TEST(ArdaTerrainValidation, StageDiagnosticsReportTextureVertexMismatch)
	{
		const FArdaTerrainSettings Settings;
		const auto SettingsBytes = TerrainSettingsBytes(Settings);
		auto Vertices = TerrainFirstCellBytes(0.42f);
		const float Zero = 0.0f;
		std::memcpy(Vertices.data() + sizeof(FArdaTerrainVertex) + offsetof(FArdaTerrainVertex, mHeight),
		    &Zero,
		    sizeof(Zero));
		const auto Summary = DescribeTerrainStageReadbacks(SettingsBytes,
		    SettingsBytes,
		    TerrainHeightmapBytes(0.25f),
		    TerrainHeightmapBytes(0.42f),
		    Vertices,
		    Settings);
		EXPECT_NE(Summary.find("finite nonzero=409600, finite min=0.25, max=0.25"), eastl::string::npos);
		EXPECT_NE(Summary.find("eroded texture/vertex matches=no, mismatched corners=1, missing corners=0"),
		    eastl::string::npos);
	}

	TEST(ArdaTerrainValidation, StageDiagnosticsIdentifyChangedSettingsCopy)
	{
		const FArdaTerrainSettings Expected;
		auto Changed = Expected;
		Changed.mFrequency = 0.0f;
		const auto Summary = DescribeTerrainStageReadbacks(TerrainSettingsBytes(Expected),
		    TerrainSettingsBytes(Changed),
		    {},
		    {},
		    {},
		    Expected);
		EXPECT_NE(Summary.find("Settings upload: bytes=32/32, matches expected=yes"), eastl::string::npos);
		EXPECT_NE(Summary.find("Settings copy: bytes=32/32, matches expected=no; width=640, height=640, frequency=0"),
		    eastl::string::npos);
		EXPECT_NE(Summary.find("Settings upload/copy bytes match=no"), eastl::string::npos);
	}

	TEST(ArdaTerrainValidation, StageDiagnosticsSafelyDescribeShortReadbacks)
	{
		const eastl::vector<uint8_t> Short(3, 0);
		const auto Summary = DescribeTerrainStageReadbacks(Short, {}, Short, {}, Short, {});
		EXPECT_NE(Summary.find("Settings upload: bytes=3/32, matches expected=no; fields unavailable"),
		    eastl::string::npos);
		EXPECT_NE(Summary.find("bytes=3/1638400, complete=no; finite=0/0"), eastl::string::npos);
		EXPECT_NE(Summary.find("finite min/max unavailable"), eastl::string::npos);
		EXPECT_NE(Summary.find("[missing, missing, missing, missing]"), eastl::string::npos);
		EXPECT_NE(Summary.find("eroded texture/vertex matches=unavailable, mismatched corners=0, missing corners=4"),
		    eastl::string::npos);
	}

	TEST(ArdaTerrainValidation, StageDiagnosticsCountNonfiniteTexelsSeparately)
	{
		const float Values[] = {1.0f,
		    -1.0f,
		    std::numeric_limits<float>::infinity(),
		    std::numeric_limits<float>::quiet_NaN()};
		eastl::vector<uint8_t> Bytes(sizeof(Values));
		std::memcpy(Bytes.data(), Values, sizeof(Values));
		const auto Summary = DescribeTerrainStageReadbacks({}, {}, Bytes, {}, {}, {});
		EXPECT_NE(Summary.find("complete=no; finite=2/4, finite nonzero=2, finite min=-1, max=1"), eastl::string::npos);
	}

	TEST(ArdaTerrainValidation, GradientFailureIncludesCellCoordinatesAndCornerHeights)
	{
		const auto Readback = MakeTerrain(
		    [](float U, float V)
		    {
			    return U == 0.0f && V == 0.0f ? 0.42f : 0.0f;
		    });
		const auto Error = ValidateTerrainReadback(Readback.mVertices, Readback.mIndices, {});
		EXPECT_NE(Error.find("discontinuous height gradient of 0.420000 at cell 0 (x=0, y=0)"), eastl::string::npos);
		EXPECT_NE(Error.find("corner heights [00,10,01,11]=["), eastl::string::npos);
		EXPECT_NE(Error.find(", 0, 0, 0]"), eastl::string::npos);
	}
}
