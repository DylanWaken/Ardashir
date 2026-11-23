/** @file ArdaCudaConfig.h
 * CUDA scheduling policy, qualified execution mode and device capabilities.
 */
#pragma once

#include <EASTL/string.h>

#include <cstdint>

namespace arda
{
	/** Selects CUDA scheduling once, before resources and command lists are created. */
	enum class EArdaCudaExecutionMode : uint8_t
	{
		/** Prefer graphics-queue execution; fall back when context or surface qualification fails. */
		Automatic,
		/** Require the provider's native graphics-queue CUDA path. */
		GraphicsQueue,
		/** Use an ordinary CUDA context and serialize graphics/CUDA submission segments. */
		ContextSwitch
	};

	/** The qualified native execution path selected for this device. */
	enum class EArdaCudaLaunchMode : uint8_t
	{
		/** CUDA is disabled, unavailable, or rejected by device admission. */
		None,
		/** CUDA in Graphics capture on the D3D12 graphics queue. */
		D3D12CiG,
		/** CUDA stream joined to a Vulkan external compute queue. */
		VulkanCiG,
		/** An ordinary CUDA context; graphics and CUDA segments execute separately. */
		ContextSwitch
	};

	/** Qualified launch mode and limits for this device, independent of graphics capabilities. */
	struct FArdaCudaCapabilities
	{
		/** None disables CUDA selection without disabling graphics compute. */
		EArdaCudaLaunchMode mLaunchMode = EArdaCudaLaunchMode::None;
		/** Explains why automatic selection used a context-switching fallback. */
		eastl::string mFallbackReason;
		/** CUDA architecture encoded as major * 10 + minor; SM 12.0 is 120. */
		uint32_t mComputeCapability = 0;
		/** Maximum product of the three block dimensions. */
		uint32_t mMaxThreadsPerBlock = 0;
		/** Inclusive per-axis limits for block dimensions, in threads. */
		uint32_t mMaxBlockSize[3] = {};
		/** Inclusive per-axis limits for grid dimensions, in blocks. */
		uint32_t mMaxGridSize[3] = {};
		/** Maximum explicitly requested dynamic shared memory per block, in bytes. */
		uint32_t mMaxSharedMemoryBytes = 0;
		/** True only when native surface mapping/handles have been qualified. */
		bool mbSurfaceAccess = false;
		/** True when layered CUDA surfaces are qualified in addition to ordinary surfaces. */
		bool mbLayeredSurfaceAccess = false;
		/** Diagnostic explaining surface exclusion; buffer launches may still work. */
		eastl::string mSurfaceUnavailableReason = "CUDA surfaces were not enabled by this provider.";
		/** Diagnostic explaining why no CUDA launch mode is available. */
		eastl::string mUnavailableReason = "CUDA launch support was not enabled by this provider.";

		/** True when this device admits a CUDA launch mode; does not imply surface support. */
		explicit operator bool() const noexcept
		{
			return mLaunchMode != EArdaCudaLaunchMode::None;
		}
	};
}
