// Documentation snapshot of Source/ArdaTests/Examples/PixelSort/Public/Nodes/ArdaPixelSortOperand.h.
#pragma once

#include "ArdaPixelSortKernelParameters.h"
#include "NativeCompute/ArdaNativeCompute.h"

namespace arda
{
	inline constexpr char ArdaPixelSortOperandId[] = "PixelSort.Radix";

	FArdaRHIStatus EnsurePixelSortOperandRegistered();
	FArdaRHIStatus CheckPixelSortNativeSupport(const IArdaRHIDevice* Device);
	FArdaNativeInvocation MakePixelSortInvocation(FArdaRHITextureRef Input,
	    FArdaRHITextureRef Output,
	    FArdaPixelSortShaderConstants Constants);
}
