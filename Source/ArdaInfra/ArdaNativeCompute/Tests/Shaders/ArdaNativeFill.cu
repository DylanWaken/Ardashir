extern "C" __global__ void arda_native_fill(unsigned int* Output, unsigned int Base)
{
	Output[threadIdx.x] = Base + threadIdx.x;
}
