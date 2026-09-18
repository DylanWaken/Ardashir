#include "RHI/Resources/ArdaRHIFormat.h"

#include "ArdaHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{
	const FArdaRHIFormatInfo& GetArdaRHIFormatInfo(EArdaRHIFormat Format) noexcept
	{
		static const FArdaRHIFormatInfo Default{};
		static const FArdaRHIFormatInfo Normal1{false, false, false, 1, 1, 1};
		static const FArdaRHIFormatInfo Normal2{false, false, false, 2, 1, 1};
		static const FArdaRHIFormatInfo Normal4{false, false, false, 4, 1, 1};
		static const FArdaRHIFormatInfo Normal8{false, false, false, 8, 1, 1};
		static const FArdaRHIFormatInfo Normal12{false, false, false, 12, 1, 1};
		static const FArdaRHIFormatInfo Normal16{false, false, false, 16, 1, 1};
		static const FArdaRHIFormatInfo Integer1{false, false, true, 1, 1, 1};
		static const FArdaRHIFormatInfo Integer2{false, false, true, 2, 1, 1};
		static const FArdaRHIFormatInfo Integer4{false, false, true, 4, 1, 1};
		static const FArdaRHIFormatInfo Integer8{false, false, true, 8, 1, 1};
		static const FArdaRHIFormatInfo Integer12{false, false, true, 12, 1, 1};
		static const FArdaRHIFormatInfo Integer16{false, false, true, 16, 1, 1};
		static const FArdaRHIFormatInfo Depth2{true, false, false, 2, 1, 1};
		static const FArdaRHIFormatInfo Depth4{true, false, false, 4, 1, 1};
		static const FArdaRHIFormatInfo DepthStencil4{true, true, false, 4, 1, 1};
		static const FArdaRHIFormatInfo DepthStencil8{true, true, false, 8, 1, 1};
		static const FArdaRHIFormatInfo Block8{false, false, false, 8, 4, 4};
		static const FArdaRHIFormatInfo Block16{false, false, false, 16, 4, 4};
		switch (Format)
		{
		case EArdaRHIFormat::R8UInt:
		case EArdaRHIFormat::R8SInt:
			return Integer1;
		case EArdaRHIFormat::RG8UInt:
		case EArdaRHIFormat::RG8SInt:
		case EArdaRHIFormat::R16UInt:
		case EArdaRHIFormat::R16SInt:
			return Integer2;
		case EArdaRHIFormat::RGBA8UInt:
		case EArdaRHIFormat::RGBA8SInt:
		case EArdaRHIFormat::RG16UInt:
		case EArdaRHIFormat::RG16SInt:
		case EArdaRHIFormat::R32UInt:
		case EArdaRHIFormat::R32SInt:
			return Integer4;
		case EArdaRHIFormat::RGBA16UInt:
		case EArdaRHIFormat::RGBA16SInt:
		case EArdaRHIFormat::RG32UInt:
		case EArdaRHIFormat::RG32SInt:
			return Integer8;
		case EArdaRHIFormat::RGB32UInt:
		case EArdaRHIFormat::RGB32SInt:
			return Integer12;
		case EArdaRHIFormat::RGBA32UInt:
		case EArdaRHIFormat::RGBA32SInt:
			return Integer16;
		case EArdaRHIFormat::R8UNorm:
		case EArdaRHIFormat::R8SNorm:
			return Normal1;
		case EArdaRHIFormat::RG8UNorm:
		case EArdaRHIFormat::RG8SNorm:
		case EArdaRHIFormat::R16UNorm:
		case EArdaRHIFormat::R16SNorm:
		case EArdaRHIFormat::R16Float:
			return Normal2;
		case EArdaRHIFormat::RGBA8UNorm:
		case EArdaRHIFormat::RGBA8SNorm:
		case EArdaRHIFormat::BGRA8UNorm:
		case EArdaRHIFormat::SRGBA8UNorm:
		case EArdaRHIFormat::SBGRA8UNorm:
		case EArdaRHIFormat::R10G10B10A2UNorm:
		case EArdaRHIFormat::R11G11B10Float:
		case EArdaRHIFormat::RG16UNorm:
		case EArdaRHIFormat::RG16SNorm:
		case EArdaRHIFormat::RG16Float:
		case EArdaRHIFormat::R32Float:
			return Normal4;
		case EArdaRHIFormat::RGBA16Float:
		case EArdaRHIFormat::RGBA16UNorm:
		case EArdaRHIFormat::RGBA16SNorm:
		case EArdaRHIFormat::RG32Float:
			return Normal8;
		case EArdaRHIFormat::RGB32Float:
			return Normal12;
		case EArdaRHIFormat::RGBA32Float:
			return Normal16;
		case EArdaRHIFormat::D16:
			return Depth2;
		case EArdaRHIFormat::D24S8:
			return DepthStencil4;
		case EArdaRHIFormat::D32:
			return Depth4;
		case EArdaRHIFormat::D32S8:
			return DepthStencil8;
		case EArdaRHIFormat::BC1UNorm:
		case EArdaRHIFormat::BC1UNormSRGB:
		case EArdaRHIFormat::BC4UNorm:
		case EArdaRHIFormat::BC4SNorm:
			return Block8;
		case EArdaRHIFormat::BC2UNorm:
		case EArdaRHIFormat::BC2UNormSRGB:
		case EArdaRHIFormat::BC3UNorm:
		case EArdaRHIFormat::BC3UNormSRGB:
		case EArdaRHIFormat::BC5UNorm:
		case EArdaRHIFormat::BC5SNorm:
		case EArdaRHIFormat::BC6HUFloat:
		case EArdaRHIFormat::BC6HSFloat:
		case EArdaRHIFormat::BC7UNorm:
		case EArdaRHIFormat::BC7UNormSRGB:
			return Block16;
		default:
			return Default;
		}
	}

	uint32_t GetArdaRHIVertexFormatAlignment(EArdaRHIFormat Format) noexcept
	{
		const auto& Info = GetArdaRHIFormatInfo(Format);
		if (!IsArdaRHIFormatKnown(Format) || Info.mbDepth || Info.mBlockWidth != 1 || Info.mBlockHeight != 1)
		{
			return 0;
		}
		switch (Format)
		{
		case EArdaRHIFormat::R8UInt:
		case EArdaRHIFormat::R8SInt:
		case EArdaRHIFormat::R8UNorm:
		case EArdaRHIFormat::R8SNorm:
		case EArdaRHIFormat::RG8UInt:
		case EArdaRHIFormat::RG8SInt:
		case EArdaRHIFormat::RG8UNorm:
		case EArdaRHIFormat::RG8SNorm:
		case EArdaRHIFormat::RGBA8UInt:
		case EArdaRHIFormat::RGBA8SInt:
		case EArdaRHIFormat::RGBA8UNorm:
		case EArdaRHIFormat::RGBA8SNorm:
		case EArdaRHIFormat::BGRA8UNorm:
		case EArdaRHIFormat::SRGBA8UNorm:
		case EArdaRHIFormat::SBGRA8UNorm:
			return 1;
		case EArdaRHIFormat::R16UInt:
		case EArdaRHIFormat::R16SInt:
		case EArdaRHIFormat::R16UNorm:
		case EArdaRHIFormat::R16SNorm:
		case EArdaRHIFormat::R16Float:
		case EArdaRHIFormat::RG16UInt:
		case EArdaRHIFormat::RG16SInt:
		case EArdaRHIFormat::RG16UNorm:
		case EArdaRHIFormat::RG16SNorm:
		case EArdaRHIFormat::RG16Float:
		case EArdaRHIFormat::RGBA16UInt:
		case EArdaRHIFormat::RGBA16SInt:
		case EArdaRHIFormat::RGBA16Float:
		case EArdaRHIFormat::RGBA16UNorm:
		case EArdaRHIFormat::RGBA16SNorm:
			return 2;
		default:
			return 4;
		}
	}

	uint32_t GetArdaRHIFormatElementSize(EArdaRHIFormat Format) noexcept
	{
		const FArdaRHIFormatInfo& Info = GetArdaRHIFormatInfo(Format);
		return Info.mBlockWidth == 1 && Info.mBlockHeight == 1 ? Info.mBytesPerBlock : 0;
	}

	uint32_t GetArdaRHIFormatPlaneCount(EArdaRHIFormat Format) noexcept
	{
		return GetArdaRHIFormatInfo(Format).mbStencil ? 2u : 1u;
	}
}
