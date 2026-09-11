#include "PixelSortOperand.h"

// PROFILE pixel_sort in CMake generates the namespace arda_cuda_pixel_sort.
// This ordinary C++ declaration calls the host registration function compiled
// by nvcc. The explicit symbol reference also keeps its static-library object
// (and kernel entries) linked; an unreferenced registration object can be discarded.
namespace arda_cuda_pixel_sort
{
	void BindPixelSort(arda::FPixelSortOperand::FRegistry&);
}

namespace arda
{
	// The helper registers typed entry wrappers and payloads, not GPU work.
	// Registry errors are retained and reported when the base freezes bindings.
	void FPixelSortOperand::BindKernelVariants(FRegistry& Registry) const
	{
		arda_cuda_pixel_sort::BindPixelSort(Registry);
	}

	TArdaRHIResult<FArdaCudaKernelSelection> FPixelSortOperand::SelectKernel(const FParameters& P,
	    const FArdaCudaSelectionContext&,
	    const FVariants& Candidates) const
	{
		// The framework checks the common schema/resource contract. These checks
		// express this algorithm's additional rules: one nonempty 2D image,
		// separate source/destination textures, and an 8-bit RGB sorting key.
		// A future algorithm that accepts empty input could return NoWork instead.
		if (!P.mWidth || !P.mHeight || P.mChannel > 2 || P.mThreshold > 255 || !P.mInput.mTexture ||
		    !P.mOutput.mTexture || P.mInput.mTexture == P.mOutput.mTexture)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			        "PixelSort needs two distinct textures, valid dimensions and an RGB channel.")};
		}
		for (const auto* Texture : {P.mInput.mTexture.Get(), P.mOutput.mTexture.Get()})
		{
			const auto& D = Texture->GetDesc();
			if (D.mWidth != P.mWidth || D.mHeight != P.mHeight || D.mMipLevels != 1 ||
			    D.mDimension != EArdaRHITextureDimension::Texture2D)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				        "PixelSort dimensions must match a single 2D mip.")};
			}
		}

		// A visible policy for demonstrating selection: portrait sorts columns;
		// landscape and square sort rows. Candidates have already been filtered
		// by native architecture coverage and execution requirements. A production
		// selector may also use the context's capabilities/queue and measured costs.
		const bool Vertical = P.mHeight > P.mWidth;
		for (const auto& V : Candidates)
		{
			if (V.mPayload.mbVertical == Vertical)
			{
				FArdaCudaKernelSelection Choice;

				// Use the registry's ID, not an index in this filtered list.
				Choice.mVariantId = V.mId;
				Choice.mLaunch.mBlockSize[0] = V.mPayload.mThreads;
				const auto Length = Vertical ? P.mHeight : P.mWidth;

				// Grid X selects a tile along the sorting axis; grid Y selects
				// a row/column. Each thread handles four pixels in that tile.
				// Example 960x1200: columns.256 -> grid (2,960,1), block (256,1,1).
				// Other block/grid dimensions default to 1; dynamic shared memory
				// stays zero because RadixSort declares its shared arrays statically.
				Choice.mLaunch.mGridSize[0] = 1 + (Length - 1) / V.mPayload.mTileSize;
				Choice.mLaunch.mGridSize[1] = Vertical ? P.mWidth : P.mHeight;
				return {Choice, {}};
			}
		}

		// There is no runtime compilation fallback. Ship a profile containing
		// compatible native code and a variant implementing the selected direction.
		return {{},
		    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
		        "No native PixelSort variant supports this orientation/device.")};
	}
}
