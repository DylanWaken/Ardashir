#pragma once
#include "RHI/CUDA/ArdaCudaParameters.h"
#include <EASTL/shared_ptr.h>

namespace arda
{
	class FArdaNativeComputeRegistry;
	struct FArdaNativeInvocation;

	inline constexpr char ArdaPixelSortOperandId[] = "PixelSort.Radix";
	inline constexpr uint32_t ArdaPixelSortItemsPerThread = 4;
	inline constexpr uint32_t ArdaPixelSortRowThreads = 128;
	inline constexpr uint32_t ArdaPixelSortColumnThreads = 256;

	// Declare the physical pixel layout, not a display color type. On this x64
	// example the kernel reads these four bytes as uint32_t: R is bits 0..7,
	// G is 8..15, B is 16..23, and A is 24..31. Whole pixels move during sorting.
	struct FArdaPixelSortColor
	{
		uint8_t mR, mG, mB, mA;
	};

// One field list generates host storage, the CUDA ABI, and shader constants.
//   FArdaPixelSortParameters        : retained RHI texture views + host scalar values
//   FArdaPixelSortParameters::FArdaCuda : CUDA surface handles + copied scalar values
// SURFACE declares the exact storage format and access used by the kernel.
// VALUE declares plain launch data; do not hide resource pointers in VALUE fields.
// BUFFER is available for typed buffer views, but this example needs only surfaces.
// Runtime checks reject unsupported types, mismatched formats and allocations
// that were not created for CUDA sharing. Conversion resolves resource handles;
// it does not normalize bytes, repack channels, or convert one color format to another.
#define ARDA_PIXEL_SORT_FIELDS(VALUE, BUFFER, SURFACE)                                                                 \
	SURFACE(FArdaPixelSortColor, mInput, EArdaComputeAccess::Read, EArdaRHIFormat::RGBA8UInt)                          \
	SURFACE(FArdaPixelSortColor, mOutput, EArdaComputeAccess::Write, EArdaRHIFormat::RGBA8UInt)                        \
	VALUE(uint32_t, mWidth)                                                                                            \
	VALUE(uint32_t, mHeight)                                                                                           \
	VALUE(uint32_t, mChannel)                                                                                          \
	VALUE(uint32_t, mThreshold)
	ARDA_CUDA_PARAMETER_STRUCT(FArdaPixelSortParameters, ARDA_PIXEL_SORT_FIELDS)

	struct FArdaPixelSortShaderConstants
	{
#define ARDA_PIXEL_SORT_CONSTANT(T, Name) T Name;
#define ARDA_PIXEL_SORT_SKIP_RESOURCE(...)
		ARDA_PIXEL_SORT_FIELDS(ARDA_PIXEL_SORT_CONSTANT, ARDA_PIXEL_SORT_SKIP_RESOURCE, ARDA_PIXEL_SORT_SKIP_RESOURCE)
#undef ARDA_PIXEL_SORT_SKIP_RESOURCE
#undef ARDA_PIXEL_SORT_CONSTANT
	};
#undef ARDA_PIXEL_SORT_FIELDS

	FArdaRHIStatus CheckPixelSortNativeSupport(const IArdaRHIDevice* Device);
	TArdaRHIResult<eastl::shared_ptr<const FArdaNativeComputeRegistry>> GetPixelSortRegistry();
	TArdaRHIResult<FArdaNativeInvocation> MakePixelSortInvocation(const FArdaPixelSortParameters& Parameters);
}
