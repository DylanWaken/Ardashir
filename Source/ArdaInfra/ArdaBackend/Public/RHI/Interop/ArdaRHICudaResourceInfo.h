/** @file ArdaRHICudaResourceInfo.h
 * Declares CudaResourceInfo definitions for the RHI interop module.
 */

#pragma once


#include <cstdint>

namespace arda
{
	/** Representation the qualified allocation can supply to a CUDA kernel. */
	enum class EArdaCudaRepresentation : uint8_t
	{
		/** No compatible representation, including opaque acceleration-structure storage. */
		None,
		/** Linear device address for a buffer range. */
		LinearBuffer,
		/** CUDA surface object/storage-image handle for one texture mip. */
		Surface
	};

	/** Available API representations, independent of queue ownership and resource access state. */
	enum class EArdaResourceRepresentations : uint8_t
	{
		/** Only the graphics representation was created. */
		Graphics,
		/** Both representations exist and refer to the same native allocation. */
		GraphicsAndCuda
	};

	/** Representation availability is not permission for concurrent access. D3D12 CiG captures launches into
     * the graphics command list. Vulkan CiG and ordinary contexts launch on CUDA streams ordered with graphics
     * by GPU fence/semaphore handoffs. Retain shared storage until the final graphics consumer completes. */
	struct FArdaCudaResourceInfo
	{
		/** Allocation/device-qualified representation, not a promise for every resource of this type. */
		EArdaCudaRepresentation mSupportedRepresentation = EArdaCudaRepresentation::None;
		/** Whether this allocation actually owns a CUDA representation alongside graphics. */
		EArdaResourceRepresentations mRepresentations = EArdaResourceRepresentations::Graphics;
		/** True after successful native allocation/mapping; callers cannot toggle this state. */
		bool mbSharingEnabled = false;
	};
}
