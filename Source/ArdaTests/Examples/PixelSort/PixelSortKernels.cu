#include "PixelSortOperand.h"
// Generated for this CMake profile: namespace, build identity, native SM coverage
// and compiler-policy metadata. Do not fabricate coverage in the binding function.
#include "ArdaCudaBuildInfo.h"
// Include the nvcc binding adapter only in CUDA translation units. It wraps the
// compiled entry and later launches it with cudaLaunchKernel on a provider stream.
#include "Compute/ArdaCudaKernelBinding.cuh"

namespace ARDA_CUDA_BUILD_NAMESPACE
{
	// A stable least-significant-digit (LSD) radix sort of (run ID, channel key).
	// Dark pixels delimit bright runs and remain at their original positions.
	// Sorting stops at tile edges: this is an image effect, not a whole-image sort.
	// Independent tiles need no cross-block synchronization or second kernel launch.
	//
	// Every registered entry takes exactly FPixelSortParameters::FCuda by value.
	// P's texture members are provider-resolved CUDA surface objects, not pointers
	// to the host RHI wrappers. All variants share this ABI despite template choices.
	template <int Threads, bool Vertical>
	__global__ void RadixSort(arda::FPixelSortParameters::FCuda P)
	{
		constexpr int Items = 4, Tile = Threads * Items, Warps = Threads / 32;

		// Four stripes of Threads pixels cover the tile in linear pixel order.
		// The key uses 9 low bits (boundary=0, bright=channel+1 in 1..256),
		// plus enough high bits for an inclusive boundary count up to Tile.
		// These two supported tile sizes therefore need 19 or 20 radix passes.
		constexpr int Groups = Warps * Items, Bits = Threads == 128 ? 19 : 20;

		// Ping-pong colors and keys between stable partition passes. Shared storage
		// is about 8 KiB for 128 threads, 16 KiB for 256, plus prefix counters.
		__shared__ uint32_t Colors[Tile], Keys[Tile], NextColors[Tile], NextKeys[Tile];
		__shared__ uint32_t Prefix[Groups], Total;
		const uint32_t Thread = threadIdx.x, Lane = Thread & 31;
		const uint32_t PriorLanes = (1u << Lane) - 1u;
		uint32_t Pixels[Items], Masks[Items];
		const uint32_t Length = Vertical ? P.mHeight : P.mWidth;
		const uint32_t Start = blockIdx.x * Tile;

		// 1. Load pixels and find boundaries. All threads participate even in a
		// partial edge tile: an early return would break ballots/block barriers.
		// Surface X coordinates are byte offsets, hence X * 4 for RGBA8UInt.
		for (int I = 0; I < Items; ++I)
		{
			const uint32_t Index = Thread + I * Threads, Position = Start + Index;
			const uint32_t X = Vertical ? blockIdx.y : Position, Y = Vertical ? Position : blockIdx.y;
			uint32_t Pixel = 0;
			if (Position < Length)
			{
				Pixel = surf2Dread<uint32_t>(P.mInput, X * 4, Y);
			}
			Pixels[I] = Pixel;

			// Integer luminance matches the CPU reference exactly. Invalid tail
			// lanes count as boundaries now and receive maximal keys below.
			const uint32_t Luma = (54 * (Pixel & 255) + 183 * ((Pixel >> 8) & 255) + 19 * ((Pixel >> 16) & 255)) >> 8;
			Masks[I] = __ballot_sync(0xffffffffu, Position >= Length || Luma < P.mThreshold);
			if (Lane == 0)
			{
				Prefix[I * Warps + Thread / 32] = __popc(Masks[I]);
			}
		}
		__syncthreads();

		// Prefix contains one boundary count per warp per stripe. Thread zero
		// scans these small counts; each lane then adds its local ballot prefix.
		// The ordering I * Warps + warp matches Thread + I * Threads pixel order.
		if (Thread == 0)
		{
			uint32_t Sum = 0;
			for (int G = 0; G < Groups; ++G)
			{
				const auto N = Prefix[G];
				Prefix[G] = Sum;
				Sum += N;
			}
		}
		__syncthreads();
		for (int I = 0; I < Items; ++I)
		{
			const auto Index = Thread + I * Threads;
			const auto Run = Prefix[I * Warps + Thread / 32] + __popc(Masks[I] & (PriorLanes | (1u << Lane)));
			const bool Boundary = (Masks[I] & (1u << Lane)) != 0;
			Colors[Index] = Pixels[I];

			// The inclusive count puts a dark boundary at the start of a new run.
			// Its low key is zero, so it precedes that run's bright pixels. Sorting
			// keeps run order and run lengths, which leaves each boundary in place.
			// Equal channel values retain their original RGBA order (stable sort).
			// Padding's low Bits bits are all ones, after every valid composite key.
			Keys[Index] = Start + Index >= Length
			    ? 0xffffffffu
			    : (Run << 9) | (Boundary ? 0u : 1u + ((Pixels[I] >> (P.mChannel * 8)) & 255));
		}
		__syncthreads();

		// 2. Stable binary partitions, from the least to most significant key bit.
		// Ballot/popcount supplies each lane's rank without shared-memory atomics.
		for (int Bit = 0; Bit < Bits; ++Bit)
		{
			for (int I = 0; I < Items; ++I)
			{
				Masks[I] = __ballot_sync(0xffffffffu, ((Keys[Thread + I * Threads] >> Bit) & 1u) == 0);
				if (Lane == 0)
				{
					Prefix[I * Warps + Thread / 32] = __popc(Masks[I]);
				}
			}
			__syncthreads();
			if (Thread == 0)
			{
				// Convert group zero counts to exclusive prefixes; Total is the
				// number of zero-bit entries, where the one-bit partition begins.
				uint32_t Sum = 0;
				for (int G = 0; G < Groups; ++G)
				{
					const auto N = Prefix[G];
					Prefix[G] = Sum;
					Sum += N;
				}
				Total = Sum;
			}
			__syncthreads();
			for (int I = 0; I < Items; ++I)
			{
				const auto Index = Thread + I * Threads;
				const auto ZerosBefore = Prefix[I * Warps + Thread / 32] + __popc(Masks[I] & PriorLanes);
				const bool Zero = (Masks[I] & (1u << Lane)) != 0;

				// Zero rank = zeros before me. One rank = my original index minus
				// zeros before me. These produce unique destinations and preserve
				// order inside each partition, so earlier radix passes stay valid.
				const auto Destination = Zero ? ZerosBefore : Total + Index - ZerosBefore;
				NextColors[Destination] = Colors[Index];
				NextKeys[Destination] = Keys[Index];
			}
			__syncthreads();

			// Finish all scatter writes before reloading; finish all reloads before
			// the next bit reuses shared state. No block consumes another block's data.
			for (int I = 0; I < Items; ++I)
			{
				const auto Index = Thread + I * Threads;
				Colors[Index] = NextColors[Index];
				Keys[Index] = NextKeys[Index];
			}
			__syncthreads();
		}

		// 3. Store only real pixels. The separate output surface avoids modifying
		// the original noise image used for comparison and the Tab preview.
		for (int I = 0; I < Items; ++I)
		{
			const auto Index = Thread + I * Threads, Position = Start + Index;
			if (Position < Length)
			{
				surf2Dwrite(Colors[Index],
				    P.mOutput,
				    (Vertical ? blockIdx.y : Position) * 4,
				    Vertical ? Position : blockIdx.y);
			}
		}
	}

	void BindPixelSort(arda::FPixelSortOperand::FRegistry& Registry)
	{
		// This host function runs once during operand binding. integer_sequence
		// supplies compile-time constants so nvcc instantiates both __global__
		// specializations during the project build; this loop never compiles CUDA.
		// Each BindArdaCudaKernel<&entry> has its own type before registry storage.
		// The registry validates the exact parameter signature at runtime and
		// remembers failures even when Add's return value is not inspected here.
		arda::ForEachArdaCudaPermutation(std::integer_sequence<int, 128, 256>{},
		    [&](auto Size)
		    {
			    constexpr int Threads = decltype(Size)::value;
			    constexpr bool Vertical = Threads == 256;

			    // Payload describes selection policy; GetBuildInfo describes the actual
			    // compiled images. Hardware/mode requirements can be supplied as the
			    // optional fourth binding argument when an algorithm has extra limits.
			    Registry.Add(
			        arda::BindArdaCudaKernel<&RadixSort<Threads, Vertical>>(Vertical ? "columns.256" : "rows.128",
			            arda::FPixelSortVariant{Threads, Threads * 4, Vertical},
			            GetBuildInfo()));
		    });

		// To grow a permutation set, nest ForEachArdaCudaPermutation and prune
		// invalid combinations with if constexpr. Give every entry a unique name
		// and matching payload. The current kernel's Bits calculation, selector,
		// CPU reference and title assume these two configurations; update them too.
		// Different ARCHITECTURES/DEFINITIONS/OPTIONS require separate CMake
		// profiles and explicit calls to each profile's BindPixelSort export.
	}
}
