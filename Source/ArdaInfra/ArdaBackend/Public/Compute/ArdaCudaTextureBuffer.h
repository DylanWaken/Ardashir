/** @file ArdaCudaTextureBuffer.h
 * GPU buffer storage for processing graphics textures with pointer-based CUDA kernels.
 */
#pragma once
#include "RHI/ArdaRHIDevice.h"

namespace arda
{
	/** One texture region represented by a CUDA-shared linear buffer.
	 * Kernels bind mBuffer with BUFFER parameters and use the byte row/slice pitches.
	 * Copy texture data in before reading or partially updating it, then copy writable
	 * results back after the complete CUDA sequence. Padding bytes are not texels.
	 */
	struct FArdaCudaTextureBuffer
	{
		/** Device-local shared storage, retained by ordinary buffer bindings and copy commands. */
		FArdaRHIBufferRef mBuffer;
		/** Buffer copy offset and byte distance between rows. */
		FArdaRHITextureBufferLayout mLayout;
		/** Region dimensions in texels; depth is one for a single array layer. */
		FArdaRHITextureCopyExtent mExtent;
		/** Byte distance between adjacent depth slices. */
		uint64_t mSlicePitch = 0;
	};

	/** Allocates reusable CUDA-shared storage for one mip/layer region, including 3D regions.
	 * The graphics texture itself need not enable CUDA interop. Uncompressed CUDA-compatible
	 * color formats are accepted; no surface-object support is required. This allocates storage
	 * only: use CopyTextureToBuffer/CopyBufferToTexture around the CUDA sequence.
	 */
	[[nodiscard]] TArdaRHIResult<FArdaCudaTextureBuffer> CreateArdaCudaTextureBuffer(IArdaRHIDevice& Device,
	    const FArdaRHITextureDesc& Texture,
	    const FArdaRHITextureSlice& Slice = {});
}
