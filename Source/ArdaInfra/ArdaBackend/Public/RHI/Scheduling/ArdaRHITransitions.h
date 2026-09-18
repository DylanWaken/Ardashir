/** @file ArdaRHITransitions.h
 * Declares Transitions definitions for the RHI scheduling module.
 */

#pragma once

#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Scheduling/ArdaRHIQueueTypes.h"
#include "RHI/Scheduling/ArdaRHIResourceStates.h"


namespace arda
{
	/** Explicit texture transition including expected state and pipeline domains. */
	struct FArdaRHITextureTransitionDesc
	{
		FArdaRHITextureSubresourceRange mSubresources;
		EArdaRHIResourceState mStateBefore = EArdaRHIResourceState::Unknown;
		EArdaRHIResourceState mStateAfter = EArdaRHIResourceState::Unknown;
		EArdaRHIPipeline mSourcePipelines = EArdaRHIPipeline::Graphics;
		EArdaRHIPipeline mDestinationPipelines = EArdaRHIPipeline::Graphics;
		EArdaRHITransitionFlags mFlags = EArdaRHITransitionFlags::None;
		/** Source queue for a paired queue-family release/acquire transfer. */
		EArdaRHIQueueType mSourceQueue = EArdaRHIQueueType::Graphics;
		/** Destination queue for a paired queue-family release/acquire transfer. */
		EArdaRHIQueueType mDestinationQueue = EArdaRHIQueueType::Graphics;
		/** True when this transition transfers native queue-family ownership. */
		bool mbQueueOwnershipTransfer = false;
	};

	/** Explicit buffer transition including expected state and pipeline domains. */
	struct FArdaRHIBufferTransitionDesc
	{
		EArdaRHIResourceState mStateBefore = EArdaRHIResourceState::Unknown;
		EArdaRHIResourceState mStateAfter = EArdaRHIResourceState::Unknown;
		EArdaRHIPipeline mSourcePipelines = EArdaRHIPipeline::Graphics;
		EArdaRHIPipeline mDestinationPipelines = EArdaRHIPipeline::Graphics;
		EArdaRHITransitionFlags mFlags = EArdaRHITransitionFlags::None;
		EArdaRHIQueueType mSourceQueue = EArdaRHIQueueType::Graphics;
		EArdaRHIQueueType mDestinationQueue = EArdaRHIQueueType::Graphics;
		bool mbQueueOwnershipTransfer = false;
	};
}
