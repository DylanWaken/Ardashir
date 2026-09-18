/** @file ArdaRHIFixedFunctionStates.h
 * Declares FixedFunctionStates definitions for the RHI pipelines module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIResource.h"

#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Maximum number of simultaneous render targets. */
	inline constexpr uint32_t ArdaRHIMaxRenderTargets = 8;

	/** Enumerates primitive topology values. */
	enum class EArdaRHIPrimitiveTopology : uint8_t
	{
		PointList,
		LineList,
		LineStrip,
		TriangleList,
		TriangleStrip,
		PatchList
	};

	/** Enumerates fill mode values. */
	enum class EArdaRHIFillMode : uint8_t
	{
		Solid,
		Wireframe
	};

	/** Enumerates cull mode values. */
	enum class EArdaRHICullMode : uint8_t
	{
		Back,
		Front,
		None
	};

	/** Enumerates comparison func values. */
	enum class EArdaRHIComparisonFunc : uint8_t
	{
		Never,
		Less,
		Equal,
		LessOrEqual,
		Greater,
		NotEqual,
		GreaterOrEqual,
		Always
	};

	/** Enumerates blend factor values. */
	enum class EArdaRHIBlendFactor : uint8_t
	{
		Zero,
		One,
		SourceColor,
		InverseSourceColor,
		SourceAlpha,
		InverseSourceAlpha,
		DestinationAlpha,
		InverseDestinationAlpha,
		DestinationColor,
		InverseDestinationColor
	};

	/** Describes raster state. */
	struct FArdaRHIRasterState
	{
		/** Stores the fill mode. */
		EArdaRHIFillMode mFillMode = EArdaRHIFillMode::Solid;
		/** Stores the cull mode. */
		EArdaRHICullMode mCullMode = EArdaRHICullMode::Back;
		/** Stores the front counter clockwise. */
		bool mbFrontCounterClockwise = false;
		/** Stores the depth clip. */
		bool mbDepthClip = true;
		/** Stores the scissor. */
		bool mbScissor = false;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIRasterState& O) const noexcept
		{
			return mFillMode == O.mFillMode && mCullMode == O.mCullMode &&
			    mbFrontCounterClockwise == O.mbFrontCounterClockwise && mbDepthClip == O.mbDepthClip &&
			    mbScissor == O.mbScissor;
		}
	};

	/** Describes depth stencil state. */
	struct FArdaRHIDepthStencilState
	{
		/** Stores the depth test. */
		bool mbDepthTest = true;
		/** Stores the depth write. */
		bool mbDepthWrite = true;
		/** Stores the depth func. */
		EArdaRHIComparisonFunc mDepthFunc = EArdaRHIComparisonFunc::Less;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIDepthStencilState& O) const noexcept
		{
			return mbDepthTest == O.mbDepthTest && mbDepthWrite == O.mbDepthWrite && mDepthFunc == O.mDepthFunc;
		}
	};

	/** Describes blend target state. */
	struct FArdaRHIBlendTargetState
	{
		/** Stores the enable. */
		bool mbEnable = false;
		/** Stores the source color. */
		EArdaRHIBlendFactor mSourceColor = EArdaRHIBlendFactor::One;
		/** Stores the destination color. */
		EArdaRHIBlendFactor mDestinationColor = EArdaRHIBlendFactor::Zero;
		/** Stores the source alpha. */
		EArdaRHIBlendFactor mSourceAlpha = EArdaRHIBlendFactor::One;
		/** Stores the destination alpha. */
		EArdaRHIBlendFactor mDestinationAlpha = EArdaRHIBlendFactor::Zero;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIBlendTargetState& O) const noexcept
		{
			return mbEnable == O.mbEnable && mSourceColor == O.mSourceColor &&
			    mDestinationColor == O.mDestinationColor && mSourceAlpha == O.mSourceAlpha &&
			    mDestinationAlpha == O.mDestinationAlpha;
		}
	};

	/** Describes blend state. */
	struct FArdaRHIBlendState
	{
		/** Stores the targets. */
		FArdaRHIBlendTargetState mTargets[ArdaRHIMaxRenderTargets]{};
		/** Stores the alpha to coverage. */
		bool mbAlphaToCoverage = false;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIBlendState& O) const noexcept
		{
			if (mbAlphaToCoverage != O.mbAlphaToCoverage)
			{
				return false;
			}
			for (uint32_t I = 0; I < ArdaRHIMaxRenderTargets; ++I)
			{
				if (!(mTargets[I] == O.mTargets[I]))
				{
					return false;
				}
			}
			return true;
		}
	};

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIRasterState& Value) noexcept;

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIDepthStencilState& Value) noexcept;

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIBlendTargetState& Value) noexcept;

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIBlendState& Value) noexcept;

	/** Interface for raster state. */
	class IArdaRHIRasterState : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIRasterState& GetDesc() const noexcept = 0;
	};

	/** Interface for blend state. */
	class IArdaRHIBlendState : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIBlendState& GetDesc() const noexcept = 0;
	};

	/** Interface for depth stencil state. */
	class IArdaRHIDepthStencilState : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIDepthStencilState& GetDesc() const noexcept = 0;
	};
}
