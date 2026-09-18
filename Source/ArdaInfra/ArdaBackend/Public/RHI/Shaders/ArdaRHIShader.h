/** @file ArdaRHIShader.h
 * Declares Shader definitions for the RHI shaders module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIResource.h"

#include <EASTL/string.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Enumerates shader stage values. */
	enum class EArdaRHIShaderStage : uint16_t
	{
		None = 0,
		Vertex = 1u << 0,
		Hull = 1u << 1,
		Domain = 1u << 2,
		Geometry = 1u << 3,
		Pixel = 1u << 4,
		Compute = 1u << 5,
		Amplification = 1u << 6,
		Mesh = 1u << 7,
		RayGeneration = 1u << 8,
		AnyHit = 1u << 9,
		ClosestHit = 1u << 10,
		Miss = 1u << 11,
		Intersection = 1u << 12,
		Callable = 1u << 13,
		WorkGraph = 1u << 14,
		AllGraphics = 0x00df,
		AllRayTracing = 0x3f00,
		All = 0x7fff
	};

	constexpr EArdaRHIShaderStage operator|(EArdaRHIShaderStage A, EArdaRHIShaderStage B) noexcept
	{
		return static_cast<EArdaRHIShaderStage>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIShaderStage operator&(EArdaRHIShaderStage A, EArdaRHIShaderStage B) noexcept
	{
		return static_cast<EArdaRHIShaderStage>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIShaderStage& operator|=(EArdaRHIShaderStage& A, EArdaRHIShaderStage B) noexcept
	{
		A = A | B;
		return A;
	}
	constexpr bool HasAnyFlags(EArdaRHIShaderStage Value, EArdaRHIShaderStage Flags) noexcept
	{
		return static_cast<uint32_t>(Value & Flags) != 0;
	}

	/** @return True when Stage names exactly one ray-tracing shader stage. */
	[[nodiscard]] inline constexpr bool IsArdaRHIRayTracingShaderStage(EArdaRHIShaderStage Stage) noexcept
	{
		const uint16_t Value = static_cast<uint16_t>(Stage);
		const uint16_t RayStages = static_cast<uint16_t>(EArdaRHIShaderStage::AllRayTracing);
		return Value != 0 && (Value & (Value - 1)) == 0 && (Value & RayStages) == Value;
	}

	/** Describes shader desc. */
	struct FArdaRHIShaderDesc
	{
		/** Stores the stage. */
		EArdaRHIShaderStage mStage = EArdaRHIShaderStage::None;
		/** Stores the bytecode. */
		const void* mBytecode = nullptr;
		/** Stores the bytecode size. */
		size_t mBytecodeSize = 0;
		/** Stores the entry point. */
		eastl::string mEntryPoint = "main";
		/** Stores the debug name. */
		eastl::string mDebugName;
	};

	/** Interface for shader. */
	class IArdaRHIShader : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the stage.
         * @return The requested value.
         */
		[[nodiscard]] virtual EArdaRHIShaderStage GetStage() const noexcept = 0;

		/**
         * Returns a deterministic identity derived from bytecode, stage, and
         * entry point for persistent pipeline-cache keys.
         */
		[[nodiscard]] virtual uint64_t GetPersistentCacheHash() const noexcept
		{
			return 0;
		}
	};

	/** Interface for shader library. */
	class IArdaRHIShaderLibrary : public virtual IArdaRHIResource
	{
	};
}
