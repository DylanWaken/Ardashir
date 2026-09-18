#include "ArdaTestComputeOperand.h"
#include "RHI/CUDA/ArdaCudaKernelBinding.cuh"
#include "ArdaCudaBuildInfo.h"
#include "ArdaCudaOperand.cuh"

namespace ARDA_CUDA_BUILD_NAMESPACE
{
	void BindAdd(arda::FArdaAddOperand::FArdaRegistry& Registry)
	{
		arda::ForEachArdaCudaPermutation(eastl::integer_sequence<int, 32, 128>{},
		    [&](auto Tile)
		    {
			    constexpr int Block = decltype(Tile)::value;
			    const auto Name = GetBuildInfo().mName + (Block == 32 ? ".block32" : ".block128");
			    Registry.Add(arda::BindArdaCudaKernel<&Add<Block>>(Name.c_str(),
			        arda::FArdaAddVariantInfo{Block},
			        GetBuildInfo()));
		    });
	}

	void BindSurface(arda::FArdaSurfaceOperand::FArdaRegistry& Registry)
	{
		Registry.Add(arda::BindArdaCudaKernel<&FillSurface>("surface.u32", 8u, GetBuildInfo()));
	}
}
