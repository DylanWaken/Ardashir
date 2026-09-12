#pragma once
// Start here when writing your own CUDA operand. This header is shared by the
// node implementation and the nvcc translation unit; CUDA SDK types stay in .cu.
#include "Compute/ArdaComputeOperand.h"

namespace arda
{
	// Declare the physical pixel layout, not a display color type. On this x64
	// example the kernel reads these four bytes as uint32_t: R is bits 0..7,
	// G is 8..15, B is 16..23, and A is 24..31. Whole pixels move during sorting.
	struct FArdaPixelSortColor
	{
		uint8_t mR, mG, mB, mA;
	};

// One field list generates both sides of the launch ABI:
//   FArdaPixelSortParameters        : retained RHI texture views + host scalar values
//   FArdaPixelSortParameters::FCuda : CUDA surface handles + copied scalar values
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

	// Per-variant host policy data. This payload is inspected by SelectKernel;
	// it is not an extra kernel argument. The .cu template fixes these values
	// in compiled code, so the registered payload must describe that code exactly.
	struct FArdaPixelSortVariant
	{
		uint32_t mThreads, mTileSize;
		bool mbVertical;
	};

	// The author supplies only binding and selection. The base owns final
	// Dispatch/DispatchDeferred implementations, parameter freezing, validation
	// and recording. Each successful nonempty dispatch selects one kernel.
	class FArdaPixelSortOperand final : public TArdaComputeOperand<FArdaPixelSortParameters, FArdaPixelSortVariant>
	{
	public:
		using TArdaComputeOperand::TArdaComputeOperand;

		const char* GetName() const noexcept override
		{
			return "PixelSort.Radix";
		}

		// Called once per operand, including when GetOperandSupport is queried.
		// Register all compiled choices here; do not inspect a frame or launch work.
		void BindKernelVariants(FRegistry&) const override;

		// Called for each dispatch with compatible variants and host parameters.
		// Return an ID and grid/block sizes; selection must have no side effects.
		TArdaRHIResult<FArdaCudaKernelSelection> SelectKernel(const FParameters&,
		    const FArdaCudaSelectionContext&,
		    const FVariants&) const override;
	};
}
