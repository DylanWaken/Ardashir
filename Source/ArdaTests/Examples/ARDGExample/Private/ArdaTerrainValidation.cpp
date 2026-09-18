#include "ArdaTerrainValidation.h"

#include <EASTL/algorithm.h>
#include <cmath>
#include <cstdio>

namespace arda
{
	namespace
	{
		float Lerp(float A, float B, float T)
		{
			return A + (B - A) * T;
		}

		float Smoothstep(float Low, float High, float Value)
		{
			const float T = eastl::clamp((Value - Low) / (High - Low), 0.0f, 1.0f);
			return T * T * (3.0f - 2.0f * T);
		}

		float Hash(int32_t X, int32_t Y, int32_t Z)
		{
			uint32_t Value = 2166136261u;
			for (const int32_t Coordinate : {X, Y, Z})
			{
				Value = (Value ^ static_cast<uint32_t>(Coordinate)) * 16777619u;
			}
			Value ^= Value >> 16;
			Value *= 0x7feb352du;
			Value ^= Value >> 15;
			Value *= 0x846ca68bu;
			Value ^= Value >> 16;
			return static_cast<float>(Value >> 8) / 16777216.0f;
		}

		float ValueNoise(float X, float Y, float Z)
		{
			const auto IX = static_cast<int32_t>(std::floor(X));
			const auto IY = static_cast<int32_t>(std::floor(Y));
			const auto IZ = static_cast<int32_t>(std::floor(Z));
			const float TX = Smoothstep(0.0f, 1.0f, X - static_cast<float>(IX));
			const float TY = Smoothstep(0.0f, 1.0f, Y - static_cast<float>(IY));
			const float TZ = Smoothstep(0.0f, 1.0f, Z - static_cast<float>(IZ));
			const float Lower = Lerp(Lerp(Hash(IX, IY, IZ), Hash(IX + 1, IY, IZ), TX),
			    Lerp(Hash(IX, IY + 1, IZ), Hash(IX + 1, IY + 1, IZ), TX),
			    TY);
			const float Upper = Lerp(Lerp(Hash(IX, IY, IZ + 1), Hash(IX + 1, IY, IZ + 1), TX),
			    Lerp(Hash(IX, IY + 1, IZ + 1), Hash(IX + 1, IY + 1, IZ + 1), TX),
			    TY);
			return Lerp(Lower, Upper, TZ);
		}

		// Independent CPU reference for GenerateNoiseHeightmapCS and ErodeHeightmapCS.
		// Sample the actual frame settings so verification also detects stale or missing uploads.
		float ExpectedHeight(uint32_t X, uint32_t Y, const FArdaTerrainSettings& Settings)
		{
			const float U = static_cast<float>(X) / static_cast<float>(Settings.mWidth - 1);
			const float V = static_cast<float>(Y) / static_cast<float>(Settings.mHeight - 1);
			float Noise = 0.0f, TotalWeight = 0.0f, Weight = 0.55f, Frequency = Settings.mFrequency;
			for (uint32_t Octave = 0; Octave < 3; ++Octave)
			{
				Noise += ValueNoise(U * Frequency,
				             V * Frequency,
				             Settings.mTime * 0.22f + static_cast<float>(Octave) * 19.0f) *
				    Weight;
				TotalWeight += Weight;
				Frequency *= 2.03f;
				Weight *= 0.5f;
			}
			const float Original = 0.42f + (Noise / TotalWeight * 2.0f - 1.0f) * Settings.mAmplitude * 0.55f;
			const float Eroded = Original - Smoothstep(0.48f, 0.9f, Original) * 0.065f;
			const float EdgeDistance = eastl::min(eastl::min(U, 1.0f - U), eastl::min(V, 1.0f - V));
			return Lerp(0.42f, Eroded, Smoothstep(0.0f, 0.12f, EdgeDistance));
		}
	}

	eastl::string ValidateTerrainReadback(const eastl::vector<uint8_t>& VertexBytes,
	    const eastl::vector<uint8_t>& IndexBytes,
	    const FArdaTerrainSettings& Settings)
	{
		if (VertexBytes.size() != static_cast<size_t>(ArdaTerrainVertexCount) * sizeof(FArdaTerrainVertex) ||
		    IndexBytes.size() != static_cast<size_t>(ArdaTerrainIndexCount) * sizeof(uint32_t))
		{
			char Message[256]{};
			std::snprintf(Message,
			    sizeof(Message),
			    "Terrain GPU readback returned unexpected byte counts: vertices expected %zu, got %zu; indices expected %zu, got %zu.",
			    static_cast<size_t>(ArdaTerrainVertexCount) * sizeof(FArdaTerrainVertex),
			    VertexBytes.size(),
			    static_cast<size_t>(ArdaTerrainIndexCount) * sizeof(uint32_t),
			    IndexBytes.size());
			return Message;
		}

		// Validate topology and shared-cell continuity independently of the GPU triangulation path.
		const auto* Vertices = reinterpret_cast<const FArdaTerrainVertex*>(VertexBytes.data());
		const auto* Indices = reinterpret_cast<const uint32_t*>(IndexBytes.data());
		constexpr uint32_t LocalIndices[6] = {0, 2, 1, 1, 2, 3};
		float MaximumGradient = 0.0f;
		uint32_t MaximumGradientCell = 0;
		for (uint32_t Cell = 0; Cell < ArdaTerrainCellCount; ++Cell)
		{
			const uint32_t VertexBase = Cell * 4;
			const uint32_t IndexBase = Cell * 6;
			for (uint32_t Index = 0; Index < 6; ++Index)
			{
				const uint32_t Expected = VertexBase + LocalIndices[Index];
				if (Indices[IndexBase + Index] != Expected)
				{
					char Message[192]{};
					std::snprintf(Message,
					    sizeof(Message),
					    "Terrain index readback diverged at cell %u index %u: expected %u, got %u.",
					    Cell,
					    Index,
					    Expected,
					    Indices[IndexBase + Index]);
					return Message;
				}
			}

			// Check corner positions against the terrain grid and generated height values.
			const uint32_t CellX = Cell % (ArdaTerrainHeightmapWidth - 1);
			const uint32_t CellY = Cell / (ArdaTerrainHeightmapWidth - 1);
			constexpr uint32_t CornerX[4] = {0, 1, 0, 1};
			constexpr uint32_t CornerY[4] = {0, 0, 1, 1};
			for (uint32_t Corner = 0; Corner < 4; ++Corner)
			{
				const FArdaTerrainVertex& Vertex = Vertices[VertexBase + Corner];
				const float ExpectedX = ((static_cast<float>(CellY + CornerY[Corner]) /
				                             static_cast<float>(ArdaTerrainHeightmapHeight - 1)) -
				                            0.5f) *
				    1.45f;
				const float ExpectedY =
				    ((static_cast<float>(CellX + CornerX[Corner]) / static_cast<float>(ArdaTerrainHeightmapWidth - 1)) -
				        0.5f) *
				    1.45f;
				if (!std::isfinite(Vertex.mPosition[0]) || !std::isfinite(Vertex.mPosition[1]) ||
				    !std::isfinite(Vertex.mPosition[2]) || !std::isfinite(Vertex.mHeight) ||
				    std::abs(Vertex.mPosition[0] - ExpectedX) > 0.00001f ||
				    std::abs(Vertex.mPosition[1] - ExpectedY) > 0.00001f ||
				    std::abs(Vertex.mPosition[2] - (Vertex.mHeight * 0.72f - 0.32f)) > 0.00002f)
				{
					char Message[192]{};
					std::snprintf(Message,
					    sizeof(Message),
					    "Terrain vertex readback diverged at cell %u corner %u: position=(%.6f, %.6f, %.6f), height=%.6f.",
					    Cell,
					    Corner,
					    Vertex.mPosition[0],
					    Vertex.mPosition[1],
					    Vertex.mPosition[2],
					    Vertex.mHeight);
					return Message;
				}
			}

			// Bound local slopes and detect cracks where adjacent cells share an edge.
			const auto HeightDiff = [](float Left, float Right)
			{
				return std::abs(Left - Right);
			};
			const float HorizontalGradient =
			    HeightDiff(Vertices[VertexBase + 0].mHeight, Vertices[VertexBase + 1].mHeight);
			const float VerticalGradient =
			    HeightDiff(Vertices[VertexBase + 0].mHeight, Vertices[VertexBase + 2].mHeight);
			const float Gradient = eastl::max(HorizontalGradient, VerticalGradient);
			if (Gradient > MaximumGradient)
			{
				MaximumGradient = Gradient;
				MaximumGradientCell = Cell;
			}
			if (CellX + 1 < ArdaTerrainHeightmapWidth - 1)
			{
				const FArdaTerrainVertex* Right = Vertices + VertexBase + 4;
				if (HeightDiff(Vertices[VertexBase + 1].mHeight, Right[0].mHeight) > 0.000001f ||
				    HeightDiff(Vertices[VertexBase + 3].mHeight, Right[2].mHeight) > 0.000001f)
				{
					char Message[160]{};
					std::snprintf(Message,
					    sizeof(Message),
					    "Terrain readback has a horizontal height seam after cell %u.",
					    Cell);
					return Message;
				}
			}
			if (CellY + 1 < ArdaTerrainHeightmapHeight - 1)
			{
				const FArdaTerrainVertex* Below = Vertices + VertexBase + (ArdaTerrainHeightmapWidth - 1) * 4;
				if (HeightDiff(Vertices[VertexBase + 2].mHeight, Below[0].mHeight) > 0.000001f ||
				    HeightDiff(Vertices[VertexBase + 3].mHeight, Below[1].mHeight) > 0.000001f)
				{
					char Message[160]{};
					std::snprintf(Message,
					    sizeof(Message),
					    "Terrain readback has a vertical height seam after cell %u.",
					    Cell);
					return Message;
				}
			}
		}
		if (MaximumGradient > 0.10f)
		{
			char Message[160]{};
			std::snprintf(Message,
			    sizeof(Message),
			    "Terrain readback has a discontinuous height gradient of %.6f at cell %u.",
			    MaximumGradient,
			    MaximumGradientCell);
			return Message;
		}
		// Topology and continuity alone accept a zero heightmap, even when erosion writes
		// its correct boundary. Compare both edges and interior against expected generation.
		constexpr uint32_t SampleIntervals = 8;
		for (uint32_t SampleY = 0; SampleY <= SampleIntervals; ++SampleY)
		{
			for (uint32_t SampleX = 0; SampleX <= SampleIntervals; ++SampleX)
			{
				const uint32_t X = SampleX * (ArdaTerrainHeightmapWidth - 1) / SampleIntervals;
				const uint32_t Y = SampleY * (ArdaTerrainHeightmapHeight - 1) / SampleIntervals;
				const uint32_t CellX = eastl::min(X, ArdaTerrainHeightmapWidth - 2);
				const uint32_t CellY = eastl::min(Y, ArdaTerrainHeightmapHeight - 2);
				const uint32_t Corner = (X - CellX) + (Y - CellY) * 2;
				const float Actual = Vertices[(CellY * (ArdaTerrainHeightmapWidth - 1) + CellX) * 4 + Corner].mHeight;
				const float Expected = ExpectedHeight(X, Y, Settings);
				// Allow small floating-point contraction differences between CPU, DXIL and SPIR-V.
				if (std::abs(Actual - Expected) > 0.0001f)
				{
					char Message[224]{};
					std::snprintf(Message,
					    sizeof(Message),
					    "Terrain generation readback diverged at (%u, %u): expected %.6f, got %.6f (time %.6f).",
					    X,
					    Y,
					    Expected,
					    Actual,
					    Settings.mTime);
					return Message;
				}
			}
		}
		return {};
	}
}
