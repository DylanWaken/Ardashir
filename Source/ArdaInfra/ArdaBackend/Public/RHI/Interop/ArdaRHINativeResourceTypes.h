/** @file ArdaRHINativeResourceTypes.h
 * Declares NativeResourceTypes definitions for the RHI interop module.
 */

#pragma once


#include <cstdint>

namespace arda
{
	/** Identifies the backend representation of an imported native resource. */
	enum class EArdaRHINativeResourceType : uint8_t
	{
		/** Native object and payload interpreted by the selected backend module. */
		BackendDefined,
		/** Standard Direct3D 12 resource object. */
		D3D12Resource,
		VulkanImage,
		VulkanBuffer,
		D3D12AccelerationStructure,
		VulkanAccelerationStructure,
		VulkanOpacityMicromap
	};

	/** Controls whether an imported native resource remains caller-owned. */
	enum class EArdaRHINativeOwnership : uint8_t
	{
		Borrowed,
		Transferred
	};
}
