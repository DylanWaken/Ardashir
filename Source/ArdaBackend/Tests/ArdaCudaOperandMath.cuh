#pragma once
#include "ArdaCudaOperandValue.h"
__device__ inline unsigned int FinishArdaOperandValue(unsigned int Value, unsigned int Bias)
{
    return AddArdaOperandValue(Value, Bias);
}
