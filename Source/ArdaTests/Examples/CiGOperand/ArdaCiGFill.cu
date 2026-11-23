#include <cstdint>

extern "C" __global__ void arda_example_fill(std::uint32_t* Output, std::uint32_t Base)
{
	Output[threadIdx.x] = Base + threadIdx.x;
}
