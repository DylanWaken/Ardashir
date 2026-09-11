#include "PixelSortOperand.h"
#include "ArdaCudaBuildInfo.h"
#include "Compute/ArdaCudaKernelBinding.cuh"

namespace ARDA_CUDA_BUILD_NAMESPACE
{
    // A stable LSD radix sort of (run ID, channel key), preserving complete RGBA pixels.
    // Dark pixels delimit runs and stay in place. Independent tiles bound shared memory.
    template<int Threads, bool Vertical>
    __global__ void RadixSort(arda::FPixelSortParameters::FCuda P)
    {
        constexpr int Items = 4, Tile = Threads * Items, Warps = Threads / 32;
        constexpr int Groups = Warps * Items, Bits = Threads == 128 ? 19 : 20;
        __shared__ uint32_t Colors[Tile], Keys[Tile], NextColors[Tile], NextKeys[Tile];
        __shared__ uint32_t Prefix[Groups], Total;
        const uint32_t Thread = threadIdx.x, Lane = Thread & 31;
        const uint32_t PriorLanes = (1u << Lane) - 1u;
        uint32_t Pixels[Items], Masks[Items];
        const uint32_t Length = Vertical ? P.mHeight : P.mWidth;
        const uint32_t Start = blockIdx.x * Tile;
        for (int I = 0; I < Items; ++I)
        {
            const uint32_t Index = Thread + I * Threads, Position = Start + Index;
            const uint32_t X = Vertical ? blockIdx.y : Position, Y = Vertical ? Position : blockIdx.y;
            uint32_t Pixel = 0;
            if (Position < Length) Pixel = surf2Dread<uint32_t>(P.mInput, X * 4, Y);
            Pixels[I] = Pixel;
            const uint32_t Luma = (54 * (Pixel & 255) + 183 * ((Pixel >> 8) & 255) + 19 * ((Pixel >> 16) & 255)) >> 8;
            Masks[I] = __ballot_sync(0xffffffffu, Position >= Length || Luma < P.mThreshold);
            if (Lane == 0) Prefix[I * Warps + Thread / 32] = __popc(Masks[I]);
        }
        __syncthreads();
        if (Thread == 0)
        {
            uint32_t Sum = 0;
            for (int G = 0; G < Groups; ++G) { const auto N = Prefix[G]; Prefix[G] = Sum; Sum += N; }
        }
        __syncthreads();
        for (int I = 0; I < Items; ++I)
        {
            const auto Index = Thread + I * Threads;
            const auto Run = Prefix[I * Warps + Thread / 32] + __popc(Masks[I] & (PriorLanes | (1u << Lane)));
            const bool Boundary = (Masks[I] & (1u << Lane)) != 0;
            Colors[Index] = Pixels[I];
            Keys[Index] = Start + Index >= Length ? 0xffffffffu :
                (Run << 9) | (Boundary ? 0u : 1u + ((Pixels[I] >> (P.mChannel * 8)) & 255));
        }
        __syncthreads();
        for (int Bit = 0; Bit < Bits; ++Bit)
        {
            for (int I = 0; I < Items; ++I)
            {
                Masks[I] = __ballot_sync(0xffffffffu, ((Keys[Thread + I * Threads] >> Bit) & 1u) == 0);
                if (Lane == 0) Prefix[I * Warps + Thread / 32] = __popc(Masks[I]);
            }
            __syncthreads();
            if (Thread == 0)
            {
                uint32_t Sum = 0;
                for (int G = 0; G < Groups; ++G) { const auto N = Prefix[G]; Prefix[G] = Sum; Sum += N; }
                Total = Sum;
            }
            __syncthreads();
            for (int I = 0; I < Items; ++I)
            {
                const auto Index = Thread + I * Threads;
                const auto ZerosBefore = Prefix[I * Warps + Thread / 32] + __popc(Masks[I] & PriorLanes);
                const bool Zero = (Masks[I] & (1u << Lane)) != 0;
                const auto Destination = Zero ? ZerosBefore : Total + Index - ZerosBefore;
                NextColors[Destination] = Colors[Index]; NextKeys[Destination] = Keys[Index];
            }
            __syncthreads();
            for (int I = 0; I < Items; ++I)
            { const auto Index = Thread + I * Threads; Colors[Index] = NextColors[Index]; Keys[Index] = NextKeys[Index]; }
            __syncthreads();
        }
        for (int I = 0; I < Items; ++I)
        {
            const auto Index = Thread + I * Threads, Position = Start + Index;
            if (Position < Length)
                surf2Dwrite(Colors[Index], P.mOutput, (Vertical ? blockIdx.y : Position) * 4,
                    Vertical ? Position : blockIdx.y);
        }
    }
    void BindPixelSort(arda::FPixelSortOperand::FRegistry& Registry)
    {
        arda::ForEachArdaCudaPermutation(std::integer_sequence<int, 128, 256>{}, [&](auto Size) {
            constexpr int Threads = decltype(Size)::value;
            constexpr bool Vertical = Threads == 256;
            Registry.Add(arda::BindArdaCudaKernel<&RadixSort<Threads, Vertical>>(
                Vertical ? "columns.256" : "rows.128", arda::FPixelSortVariant{Threads, Threads * 4, Vertical}, GetBuildInfo()));
        });
    }
}
