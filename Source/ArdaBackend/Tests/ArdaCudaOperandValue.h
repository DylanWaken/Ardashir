#pragma once

#if defined(__CUDACC__)
__device__ inline unsigned int AddArdaOperandValue(unsigned int Value, unsigned int Bias)
{
    return Value + Bias;
}
#endif
