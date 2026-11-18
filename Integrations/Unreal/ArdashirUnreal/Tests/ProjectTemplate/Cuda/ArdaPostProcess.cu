#include <cuda_runtime.h>

extern "C" __global__ void ArdaPostProcess(cudaSurfaceObject_t Surface, unsigned int Width, unsigned int Height)
{
	const unsigned int X = blockIdx.x * blockDim.x + threadIdx.x;
	const unsigned int Y = blockIdx.y * blockDim.y + threadIdx.y;
	if (X >= Width || Y >= Height)
	{
		return;
	}
	float4 Color = surf2Dread<float4>(Surface, X * sizeof(float4), Y);
	Color.x += 0.125f;
	Color.y *= 0.5f;
	Color.z = 1.0f - Color.z;
	surf2Dwrite(Color, Surface, X * sizeof(float4), Y);
}
