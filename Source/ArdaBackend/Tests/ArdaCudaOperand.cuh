#pragma once
// Included after the parameter declarations and generated build profile header.
namespace ARDA_CUDA_BUILD_NAMESPACE
{
    template<int BlockSize>
    __global__ void Add(arda::FArdaAddParameters::FCuda P)
    {
        const uint32_t Index = blockIdx.x * BlockSize + threadIdx.x;
        if (Index < P.mCount) P.mOutput[Index] = P.mInput[Index] + P.mBias;
    }
    __global__ void FillSurface(arda::FArdaSurfaceParameters::FCuda P)
    {
        const uint32_t X = blockIdx.x * blockDim.x + threadIdx.x;
        const uint32_t Y = blockIdx.y * blockDim.y + threadIdx.y;
        if (X < P.mWidth && Y < P.mHeight)
            surf2Dwrite(P.mValue, static_cast<cudaSurfaceObject_t>(P.mSurface), X * sizeof(uint32_t), Y);
    }
}
