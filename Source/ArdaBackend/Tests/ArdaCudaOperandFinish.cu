// Independent translation unit, joined with the first by the user's C++ dispatch.
#include "ArdaCudaOperandMath.cuh"
extern "C" __global__ void finish_values(const unsigned int* Input, unsigned int* Output,
    unsigned int Count, unsigned int Bias)
{
    const unsigned int Index = blockIdx.x * blockDim.x + threadIdx.x;
    if (Index < Count) Output[Index] = FinishArdaOperandValue(Input[Index], Bias);
}
