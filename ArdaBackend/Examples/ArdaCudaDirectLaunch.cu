// Documentation helper: application-owned CUDA pointers and stream are required.
// This file is not part of ArdaBackendTests or an RHI resource/stream adapter.
#include <cuda_runtime.h>

namespace arda
{
	struct FAddParameters
	{
		const unsigned int* Input;
		unsigned int* Output;
		unsigned int Count;
		unsigned int Bias;
	};

	__global__ void AddArdaValues(FAddParameters P)
	{
		const unsigned int Index = blockIdx.x * blockDim.x + threadIdx.x;
		if (Index < P.Count)
		{
			P.Output[Index] = P.Input[Index] + P.Bias;
		}
	}

	// Input and Output cover Count elements on the current CUDA device.
	// They may be identical; otherwise their ranges must not overlap.
	// The caller orders graphics producers before this stream and retains
	// allocations, mappings and Stream until all submitted work completes.
	cudaError_t LaunchArdaAddTwice(const unsigned int* Input,
	    unsigned int* Output,
	    unsigned int Count,
	    cudaStream_t Stream)
	{
		if (!Input || !Output || !Count)
		{
			return cudaErrorInvalidValue;
		}
		const dim3 Block(128, 1, 1);
		const dim3 Grid(1 + (Count - 1) / Block.x, 1, 1);
		AddArdaValues<<<Grid, Block, 0, Stream>>>({Input, Output, Count, 7u});
		const cudaError_t FirstStatus = cudaGetLastError();
		if (FirstStatus != cudaSuccess)
		{
			return FirstStatus;
		}
		AddArdaValues<<<Grid, Block, 0, Stream>>>({Output, Output, Count, 11u});
		return cudaGetLastError();
	}
}
