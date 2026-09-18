/** @file ArdaCudaExternalCallContext.h
 * Borrowed CUDA execution context and its context-owned library state.
 */
#pragma once

#include "RHI/Config/ArdaCudaConfig.h"

namespace arda
{
	/** Optional context-owned library cache, such as a pool of handles or execution plans.
     * It outlives all prepared calls from its factory and is destroyed with its CUDA context
     * current before context teardown. Access is serialized by the provider, but outstanding
     * submissions still need exclusive workspace/handle leases until their retirement.
     */
	class IArdaCudaExternalCallState
	{
	public:
		/** Releases cached native state under its owning context after every prepared call retires. */
		virtual ~IArdaCudaExternalCallState() = default;
	};

	/** Borrowed native execution identity, available only inside a library adapter.
     * Never destroy/switch the context or stream. All work must be ordered on this stream.
     */
	struct FArdaCudaExternalCallContext
	{
		/** CUDA context made current by the provider for preparation and execution. */
		void* mContext = nullptr;
		/** Non-default CUDA stream owned by this recording until GPU completion. */
		void* mStream = nullptr;
		/** Qualified scheduling path; CUDA graph support alone does not imply CiG support. */
		EArdaCudaLaunchMode mLaunchMode = EArdaCudaLaunchMode::None;
		/** Matched device architecture, encoded as major * ten + minor. */
		uint32_t mComputeCapability = 0;
		/** Optional factory/context cache. Null during CreateContextState or for a stateless factory. */
		IArdaCudaExternalCallState* mState = nullptr;
	};
}
