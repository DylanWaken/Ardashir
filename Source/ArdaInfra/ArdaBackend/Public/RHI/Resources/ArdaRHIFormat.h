/** @file ArdaRHIFormat.h
 * Declares Format definitions for the RHI resources module.
 */

#pragma once


#include <cstdint>

namespace arda
{
	/** Enumerates format values. */
	enum class EArdaRHIFormat : uint8_t
	{
		Unknown,
		R8UInt,
		R8SInt,
		R8UNorm,
		R8SNorm,
		RG8UInt,
		RG8SInt,
		RG8UNorm,
		RG8SNorm,
		R16UInt,
		R16SInt,
		R16UNorm,
		R16SNorm,
		R16Float,
		RGBA8UInt,
		RGBA8SInt,
		RGBA8UNorm,
		RGBA8SNorm,
		BGRA8UNorm,
		SRGBA8UNorm,
		SBGRA8UNorm,
		R10G10B10A2UNorm,
		R11G11B10Float,
		RG16UInt,
		RG16SInt,
		RG16UNorm,
		RG16SNorm,
		RG16Float,
		R32UInt,
		R32SInt,
		R32Float,
		RGBA16UInt,
		RGBA16SInt,
		RGBA16Float,
		RGBA16UNorm,
		RGBA16SNorm,
		RG32UInt,
		RG32SInt,
		RG32Float,
		RGB32UInt,
		RGB32SInt,
		RGB32Float,
		RGBA32UInt,
		RGBA32SInt,
		RGBA32Float,
		D16,
		D24S8,
		D32,
		D32S8,
		BC1UNorm,
		BC1UNormSRGB,
		BC2UNorm,
		BC2UNormSRGB,
		BC3UNorm,
		BC3UNormSRGB,
		BC4UNorm,
		BC4SNorm,
		BC5UNorm,
		BC5SNorm,
		BC6HUFloat,
		BC6HSFloat,
		BC7UNorm,
		BC7UNormSRGB,
		/** Number of format values, including Unknown. */
		Count
	};

	/** @return True for a usable format value rather than a sentinel. */
	[[nodiscard]] inline constexpr bool IsArdaRHIFormatKnown(EArdaRHIFormat Format) noexcept
	{
		return Format > EArdaRHIFormat::Unknown && Format < EArdaRHIFormat::Count;
	}

	/** Describes format info. */
	struct FArdaRHIFormatInfo
	{
		/** Stores the depth. */
		bool mbDepth = false;
		/** Stores the stencil. */
		bool mbStencil = false;
		/** Stores the integer. */
		bool mbInteger = false;
		/** Bytes occupied by one texel or one compressed block. */
		uint32_t mBytesPerBlock = 0;
		/** Width in texels of one storage block. */
		uint32_t mBlockWidth = 1;
		/** Height in texels of one storage block. */
		uint32_t mBlockHeight = 1;
	};

	/**
     * Returns the arda rhiformat info.
     * @param Format The format.
     * @return A reference to the requested value.
     */
	[[nodiscard]] const FArdaRHIFormatInfo& GetArdaRHIFormatInfo(EArdaRHIFormat Format) noexcept;

	/** @return The byte size of one uncompressed format element, or zero for compressed/unknown formats. */
	[[nodiscard]] uint32_t GetArdaRHIFormatElementSize(EArdaRHIFormat Format) noexcept;

	/** Vertex element/stride byte alignment: component size, or packed element size.
	 * Returns zero for unknown, depth/stencil, and compressed formats. Native vertex-format
	 * support remains a separate per-device query.
	 */
	[[nodiscard]] uint32_t GetArdaRHIVertexFormatAlignment(EArdaRHIFormat Format) noexcept;

	/** @return The number of independently addressable format planes. */
	[[nodiscard]] uint32_t GetArdaRHIFormatPlaneCount(EArdaRHIFormat Format) noexcept;
}
