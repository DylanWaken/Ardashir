#include "ArdaTestComputeOperand.h"
#include "Compute/ArdaCudaKernelBinding.cuh"
#include "ArdaCudaBuildInfo.h"
#include "ArdaCudaOperand.cuh"

namespace ARDA_CUDA_BUILD_NAMESPACE
{
    void BindAdd(arda::FArdaAddOperand::FRegistry& Registry)
    {
        arda::ForEachArdaCudaPermutation(std::integer_sequence<int, 32, 128>{}, [&](auto Tile) {
            constexpr int Block = decltype(Tile)::value;
            const auto Name = GetBuildInfo().mName + (Block == 32 ? ".block32" : ".block128");
            Registry.Add(arda::BindArdaCudaKernel<&Add<Block>>(Name.c_str(),
                arda::FArdaAddVariantInfo{Block}, GetBuildInfo()));
        });
    }
    void BindSurface(arda::FArdaSurfaceOperand::FRegistry& Registry)
    {
        Registry.Add(arda::BindArdaCudaKernel<&FillSurface>("surface.u32", 8u, GetBuildInfo()));
    }
}
