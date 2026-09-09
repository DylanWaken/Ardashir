// One translation unit shared by several shape-specific operand implementations.
#include "ArdaCudaOperandValue.h"

extern "C" __global__ void add_values(const unsigned int* Input, unsigned int* Output,
    unsigned int Count, unsigned int Bias)
{
    const unsigned int Index = blockIdx.x * blockDim.x + threadIdx.x;
    if (Index < Count)
    {
        Output[Index] = AddArdaOperandValue(Input[Index], Bias);
    }
}
